#include "dinosaur_encoder_can.h"
#include "test.h"

static CAN_HandleTypeDef hcan1 = {.Instance = CAN1};
static can_t can1;
static TIM_TypeDef tim = {.CNT = 0, .ARR = 0xFFFF};
static TIM_HandleTypeDef htim = {.Instance = &tim};
static encoder_t enc;
static encoder_can_rx_t rx;

static void test_send_frame_layout(void) {
    tim.CNT = 0;
    can_init(&can1, &hcan1);
    encoder_init(&enc, &htim, 1000);
    tim.CNT = 65536 - 100; /* -100 カウント */
    fake_dwt.CYCCNT = 1000000; /* 10ms 後 (100MHz) → -0.1 回転 / 10ms = -600rpm */
    fake.tick = 10;
    encoder_update(&enc);
    CHECK_EQ(encoder_can_send(&can1, 0x300, &enc), LIB_OK);
    CHECK_EQ(fake.can_sent_count, 1);
    CHECK_EQ(fake.can_sent[0].header.StdId, 0x300);
    CHECK_EQ(fake.can_sent[0].header.DLC, 8);
    CHECK_EQ(can_get_i32(fake.can_sent[0].data, 0), -100); /* [0..3] 累積カウント */
    CHECK_NEAR(can_get_float(fake.can_sent[0].data, 4), -600.0, 0.01); /* [4..7] rpm */
}

static void test_receive(void) {
    can_init(&can1, &hcan1);
    CHECK_EQ(encoder_can_listen(&can1, 0x301, &rx), LIB_OK);
    CHECK_EQ(encoder_can_count(&rx), 0); /* まだ届いていない */
    CHECK_NEAR(encoder_can_rpm(&rx), 0.0, 1e-9);
    CHECK_EQ(encoder_can_age_ms(&rx), UINT32_MAX);

    uint8_t d[8];
    can_put_i32(d, 0, -123456);
    can_put_float(d, 4, 1500.5f);
    fake.tick = 100;
    fake_can_receive(&hcan1, 0x301, d, 8);
    fake.tick = 130;
    CHECK_EQ(encoder_can_count(&rx), -123456);
    CHECK_NEAR(encoder_can_rpm(&rx), 1500.5, 1e-3);
    CHECK_EQ(encoder_can_age_ms(&rx), 30);
    CHECK_EQ(encoder_can_count(&rx), -123456); /* 何度読んでも同じ値 */
    CHECK_EQ(fake.primask, 0);
}

static void test_null(void) {
    can_init(&can1, &hcan1);
    CHECK_EQ(encoder_can_send(&can1, 0x300, NULL), LIB_ERR_PARAM);
    CHECK_EQ(encoder_can_listen(&can1, 0x300, NULL), LIB_ERR_PARAM);
    CHECK_EQ(encoder_can_count(NULL), 0);
    CHECK_NEAR(encoder_can_rpm(NULL), 0.0, 1e-9);
    CHECK_EQ(encoder_can_age_ms(NULL), UINT32_MAX);
}

int main(void) {
    RUN(test_send_frame_layout);
    RUN(test_receive);
    RUN(test_null);
    return TEST_RESULT();
}
