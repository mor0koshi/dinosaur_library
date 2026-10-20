#include "mor0koshi_can.h"
#include "test.h"

static CAN_HandleTypeDef hcan1 = {.Instance = CAN1};
static CAN_HandleTypeDef hcan2 = {.Instance = CAN2};
static can_t can1;
static can_t can2;
static can_box_t box;

static void test_put_get_roundtrip(void) {
    uint8_t d[8] = {0};
    can_put_i16(d, 0, -1234);
    CHECK_EQ(d[0], 0x2E); /* -1234 = 0xFB2E。リトルエンディアンなので下位が先 */
    CHECK_EQ(d[1], 0xFB);
    CHECK_EQ(can_get_i16(d, 0), -1234);
    can_put_u16(d, 2, 0xBEEF);
    CHECK_EQ(can_get_u16(d, 2), 0xBEEF);
    can_put_i32(d, 4, -100000);
    CHECK_EQ(can_get_i32(d, 4), -100000);
    can_put_u32(d, 4, 0x12345678);
    CHECK_EQ(d[4], 0x78);
    CHECK_EQ(can_get_u32(d, 4), 0x12345678);
    can_put_float(d, 0, 3.5f);
    CHECK_NEAR(can_get_float(d, 0), 3.5, 1e-9);
    can_put_i8(d, 0, -5);
    CHECK_EQ(can_get_i8(d, 0), -5);
    can_put_u8(d, 1, 200);
    CHECK_EQ(can_get_u8(d, 1), 200);
}

static void test_init_configures_hardware(void) {
    CHECK_EQ(can_init(&can1, &hcan1), LIB_OK);
    CHECK_EQ(fake.can_filter_calls, 1);
    CHECK_EQ(fake.can_last_filter.FilterBank, 0);
    CHECK_EQ(fake.can_last_filter.FilterMaskIdHigh, 0); /* マスク 0 = 全部受け取る */
    CHECK_EQ(fake.can_last_filter.FilterMaskIdLow, 0);
    CHECK_EQ(fake.can_last_filter.FilterFIFOAssignment, CAN_FILTER_FIFO0);
    CHECK_EQ(fake.can_last_filter.FilterActivation, ENABLE);
    CHECK(fake.can_start_calls == 1);
    CHECK(fake.can_mcr_at_start & CAN_MCR_ABOM); /* Start の前に ABOM が立っている */
    CHECK(fake.can_notifications & CAN_IT_RX_FIFO0_MSG_PENDING);
    CHECK(fake.can_notifications & CAN_IT_TX_MAILBOX_EMPTY);
    CHECK_EQ(fake.primask, 0);
}

static void test_init_can2_uses_filter_bank_14(void) {
    CHECK_EQ(can_init(&can2, &hcan2), LIB_OK);
    CHECK_EQ(fake.can_last_filter.FilterBank, 14);
    CHECK_EQ(fake.can_last_filter.SlaveStartFilterBank, 14);
}

static void test_init_fails_when_irq_off(void) {
    fake.nvic_enabled[CAN1_TX_IRQn] = 0;
    CHECK_EQ(can_init(&can1, &hcan1), LIB_ERR_IRQ_OFF);
    CHECK_EQ(fake.last_error, LIB_ERR_IRQ_OFF);
    CHECK_EQ(fake.can_start_calls, 0);
}

static void test_init_reports_hal_error(void) {
    fake.can_start_ret = HAL_ERROR;
    CHECK_EQ(can_init(&can1, &hcan1), LIB_ERR_HAL);
    CHECK_EQ(fake.last_error, LIB_ERR_HAL);
}

static void test_listen_and_read(void) {
    uint8_t out[8];
    can_init(&can1, &hcan1);
    CHECK_EQ(can_listen(&can1, 0x123, &box), LIB_OK);
    CHECK(!can_read(&box, out)); /* まだ何も来ていない */
    CHECK(!can_peek(&box, out));
    CHECK_EQ(can_age_ms(&box), UINT32_MAX);

    fake.tick = 1000;
    uint8_t d[3] = {1, 2, 3};
    fake_can_receive(&hcan1, 0x123, d, 3);
    fake.tick = 1040;
    CHECK(can_read(&box, out));
    CHECK_EQ(out[0], 1);
    CHECK_EQ(out[2], 3);
    CHECK_EQ(out[3], 0); /* len より後ろは 0 */
    CHECK_EQ(box.len, 3);
    CHECK(!can_read(&box, out)); /* 一度読んだら、次が届くまで false */
    CHECK_EQ(can_age_ms(&box), 40);
    CHECK(can_peek(&box, out)); /* peek は新しいかどうかに関係なく読める */
    CHECK_EQ(out[1], 2);
    CHECK_EQ(fake.primask, 0);
}

static void test_unknown_id_is_ignored(void) {
    uint8_t out[8];
    can_init(&can1, &hcan1);
    can_listen(&can1, 0x123, &box);
    CHECK_EQ(can_bus_age_ms(&can1), UINT32_MAX);
    uint8_t d[1] = {9};
    fake.tick = 50;
    fake_can_receive(&hcan1, 0x124, d, 1);
    CHECK(!can_read(&box, out));
    CHECK_EQ(can_bus_age_ms(&can1), 0); /* 知らない ID でも、バスとしては受信している */
}

static int calls_a, calls_b;
static uint32_t last_id_a;

static void handler_a(uint32_t id, const uint8_t *data, uint8_t len, void *arg) {
    (void)data;
    (void)len;
    calls_a++;
    last_id_a = id;
    CHECK(arg == &calls_a);
}

static void handler_b(uint32_t id, const uint8_t *data, uint8_t len, void *arg) {
    (void)id;
    (void)data;
    (void)len;
    (void)arg;
    calls_b++;
}

static void test_subscribe_range_and_overlap(void) {
    calls_a = calls_b = 0;
    can_init(&can1, &hcan1);
    CHECK_EQ(can_subscribe_range(&can1, 0x201, 0x208, handler_a, &calls_a), LIB_OK);
    CHECK_EQ(can_subscribe(&can1, 0x205, handler_b, NULL), LIB_OK);
    uint8_t d[8] = {0};
    fake_can_receive(&hcan1, 0x201, d, 8);
    fake_can_receive(&hcan1, 0x205, d, 8); /* 範囲と単独の両方に当たる */
    fake_can_receive(&hcan1, 0x209, d, 8); /* どちらにも当たらない */
    CHECK_EQ(calls_a, 2);
    CHECK_EQ(last_id_a, 0x205);
    CHECK_EQ(calls_b, 1);
}

static void test_subscribe_rejects_bad_args_and_full_table(void) {
    can_init(&can1, &hcan1);
    CHECK_EQ(can_subscribe_range(&can1, 0x300, 0x200, handler_b, NULL), LIB_ERR_PARAM);
    CHECK_EQ(can_subscribe(&can1, 0x800, handler_b, NULL), LIB_ERR_PARAM); /* 標準 ID は 0x7FF まで */
    CHECK_EQ(can_subscribe(&can1, 0x100, NULL, NULL), LIB_ERR_PARAM);
    for (int i = 0; i < CAN_MAX_SUBS; i++) {
        CHECK_EQ(can_subscribe(&can1, (uint32_t)i, handler_b, NULL), LIB_OK);
    }
    CHECK_EQ(can_subscribe(&can1, 0x100, handler_b, NULL), LIB_ERR_FULL);
    CHECK_EQ(fake.last_error, LIB_ERR_FULL);
}

static void test_send_goes_to_mailbox(void) {
    can_init(&can1, &hcan1);
    uint8_t d[2] = {0xAA, 0xBB};
    CHECK_EQ(can_send(&can1, 0x321, d, 2), LIB_OK);
    CHECK_EQ(fake.can_sent_count, 1);
    CHECK_EQ(fake.can_sent[0].header.StdId, 0x321);
    CHECK_EQ(fake.can_sent[0].header.IDE, CAN_ID_STD);
    CHECK_EQ(fake.can_sent[0].header.RTR, CAN_RTR_DATA);
    CHECK_EQ(fake.can_sent[0].header.DLC, 2);
    CHECK_EQ(fake.can_sent[0].data[1], 0xBB);
    CHECK_EQ(fake.primask, 0);
}

static void test_send_rejects_bad_args(void) {
    static can_t not_inited;
    uint8_t d[8] = {0};
    can_init(&can1, &hcan1);
    CHECK_EQ(can_send(&can1, 0x800, d, 8), LIB_ERR_PARAM);
    CHECK_EQ(can_send(&can1, 0x100, d, 9), LIB_ERR_PARAM);
    CHECK_EQ(can_send(&can1, 0x100, NULL, 1), LIB_ERR_PARAM);
    CHECK_EQ(can_send(&not_inited, 0x100, d, 8), LIB_ERR_PARAM); /* can_init していない */
    CHECK_EQ(fake.can_sent_count, 0);
}

static void test_send_queues_when_mailboxes_full(void) {
    can_init(&can1, &hcan1);
    uint8_t d[1];
    for (int i = 0; i < 5; i++) {
        d[0] = (uint8_t)i;
        CHECK_EQ(can_send(&can1, 0x100 + (uint32_t)i, d, 1), LIB_OK);
    }
    CHECK_EQ(fake.can_sent_count, 3); /* メールボックスは 3 つ。残り 2 つは列で待つ */
    fake_can_tx_complete(&hcan1);
    CHECK_EQ(fake.can_sent_count, 4);
    fake_can_tx_complete(&hcan1);
    CHECK_EQ(fake.can_sent_count, 5);
    CHECK_EQ(fake.can_sent[3].header.StdId, 0x103); /* 積んだ順に出る */
    CHECK_EQ(fake.can_sent[4].header.StdId, 0x104);
    fake_can_tx_complete(&hcan1); /* 列が空なら何もしない */
    CHECK_EQ(fake.can_sent_count, 5);
    CHECK_EQ(fake.primask, 0);
}

static void test_send_drops_when_queue_full(void) {
    can_init(&can1, &hcan1);
    fake.can_free_mailboxes = 0; /* ケーブルが抜けて送れない状態 */
    uint8_t d[1] = {0};
    for (int i = 0; i < CAN_TXQ_LEN; i++) {
        CHECK_EQ(can_send(&can1, 0x100, d, 1), LIB_OK);
    }
    CHECK_EQ(can_send(&can1, 0x100, d, 1), LIB_ERR_FULL);
    CHECK_EQ(can_dropped_count(&can1), 1);
    CHECK_EQ(fake.last_error, LIB_ERR_FULL);
    CHECK_EQ(fake.can_sent_count, 0);
    CHECK_EQ(fake.primask, 0);
}

static void test_null_args_do_nothing(void) {
    uint8_t d[8];
    CHECK_EQ(can_init(NULL, &hcan1), LIB_ERR_PARAM);
    CHECK_EQ(can_init(&can1, NULL), LIB_ERR_PARAM);
    CHECK_EQ(can_listen(&can1, 0x10, NULL), LIB_ERR_PARAM);
    CHECK(!can_read(NULL, d));
    CHECK(!can_peek(NULL, d));
    CHECK_EQ(can_age_ms(NULL), UINT32_MAX);
    CHECK_EQ(can_bus_age_ms(NULL), UINT32_MAX);
    CHECK_EQ(can_dropped_count(NULL), 0);
}

static void test_abort_callback_pumps_queue(void) {
    can_init(&can1, &hcan1);
    uint8_t d[1];
    for (int i = 0; i < 4; i++) {
        d[0] = (uint8_t)i;
        CHECK_EQ(can_send(&can1, 0x100 + (uint32_t)i, d, 1), LIB_OK);
    }
    fake.can_free_mailboxes = 1;
    HAL_CAN_TxMailbox1AbortCallback(&hcan1);
    CHECK_EQ(fake.can_sent_count, 4);
    CHECK_EQ(fake.primask, 0);
}

static void test_init_fails_when_all_buses_used(void) {
    static CAN_HandleTypeDef hx1 = {.Instance = CAN1};
    static CAN_HandleTypeDef hx2 = {.Instance = CAN1};
    static can_t bx1, bx2;

    /* Fill slots with can1, can2, bx1 */
    CHECK_EQ(can_init(&can1, &hcan1), LIB_OK);
    CHECK_EQ(can_init(&can2, &hcan2), LIB_OK);
    CHECK_EQ(can_init(&bx1, &hx1), LIB_OK);
    /* 4th should fail */
    CHECK_EQ(can_init(&bx2, &hx2), LIB_ERR_FULL);
    CHECK_EQ(fake.last_error, LIB_ERR_FULL);
}

int main(void) {
    RUN(test_put_get_roundtrip);
    RUN(test_init_configures_hardware);
    RUN(test_init_can2_uses_filter_bank_14);
    RUN(test_init_fails_when_irq_off);
    RUN(test_init_reports_hal_error);
    RUN(test_listen_and_read);
    RUN(test_unknown_id_is_ignored);
    RUN(test_subscribe_range_and_overlap);
    RUN(test_subscribe_rejects_bad_args_and_full_table);
    RUN(test_send_goes_to_mailbox);
    RUN(test_send_rejects_bad_args);
    RUN(test_send_queues_when_mailboxes_full);
    RUN(test_send_drops_when_queue_full);
    RUN(test_null_args_do_nothing);
    RUN(test_abort_callback_pumps_queue);
    RUN(test_init_fails_when_all_buses_used);
    return TEST_RESULT();
}
