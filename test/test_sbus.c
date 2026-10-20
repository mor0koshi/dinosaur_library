#include <string.h>

#include "mor0koshi_sbus.h"
#include "test.h"

static DMA_HandleTypeDef hdma;
static UART_HandleTypeDef huart5 = {.hdmarx = &hdma};
static UART_HandleTypeDef huart_other = {.hdmarx = &hdma};
static sbus_t sbus;

/* 16ch × 11bit を SBUS のフレーム (25 バイト) に詰める (下位ビットから順に) */
static void pack_frame(const uint16_t ch[16], uint8_t flags, uint8_t f[25]) {
    memset(f, 0, 25);
    f[0] = 0x0F;
    int bit = 0;
    for (int c = 0; c < 16; c++) {
        for (int b = 0; b < 11; b++, bit++) {
            if (ch[c] & (1u << b)) {
                f[1 + bit / 8] |= (uint8_t)(1u << (bit % 8));
            }
        }
    }
    f[23] = flags;
}

/* DMA がバッファの offset バイト目からフレームを書き、UART がアイドルになった、という状況を作る */
static void receive_frame(const uint16_t ch[16], uint8_t flags, int offset) {
    uint8_t f[25];
    pack_frame(ch, flags, f);
    memcpy(&sbus.rxbuf[offset], f, 25);
    HAL_UARTEx_RxEventCallback(&huart5, (uint16_t)(offset + 25));
}

static void all_channels(uint16_t ch[16], uint16_t v) {
    for (int i = 0; i < 16; i++) {
        ch[i] = v;
    }
}

static void test_init_starts_dma(void) {
    hdma.ht_disabled = 0;
    CHECK_EQ(sbus_init(&sbus, &huart5), LIB_OK);
    CHECK_EQ(fake.uart_rx_start_calls, 1);
    CHECK(fake.uart_rx_buf == sbus.rxbuf);
    CHECK_EQ(fake.uart_rx_size, SBUS_RXBUF_LEN);
    CHECK_EQ(hdma.ht_disabled, 1); /* 半分受信の割り込みは切る */
    CHECK_EQ((uintptr_t)sbus.rxbuf % 32, 0); /* D キャッシュの都合で 32 バイト境界 */
    CHECK_EQ(fake.primask, 0);
}

static void test_init_without_dma_fails(void) {
    static UART_HandleTypeDef no_dma = {.hdmarx = NULL};
    static sbus_t other;
    CHECK_EQ(sbus_init(&other, &no_dma), LIB_ERR_IRQ_OFF);
    CHECK_EQ(fake.uart_rx_start_calls, 0);
}

static void test_init_reports_hal_error(void) {
    fake.uart_rx_ret = HAL_ERROR;
    CHECK_EQ(sbus_init(&sbus, &huart5), LIB_ERR_HAL);
}

static void test_decode_hand_made_frame(void) {
    sbus_init(&sbus, &huart5);
    sbus.rxbuf[0] = 0x0F;
    sbus.rxbuf[1] = 0x23;
    sbus.rxbuf[2] = 0x29; /* 下位 3bit (1) が ch0 の上位、上位 5bit (5) が ch1 の下位 */
    HAL_UARTEx_RxEventCallback(&huart5, 25);
    sbus_update(&sbus);
    CHECK_EQ(sbus_raw(&sbus, 0), 0x123);
    CHECK_EQ(sbus_raw(&sbus, 1), 5);
    CHECK_EQ(sbus_raw(&sbus, 2), 0);
    CHECK_EQ(sbus_frame_count(&sbus), 1);
    CHECK_EQ(fake.uart_rx_start_calls, 2); /* 受信のたびに DMA をかけ直す */
}

static void test_decode_all_channels(void) {
    uint16_t ch[16];
    for (int i = 0; i < 16; i++) {
        ch[i] = (uint16_t)(100 * i + 7);
    }
    sbus_init(&sbus, &huart5);
    receive_frame(ch, 0, 0);
    sbus_update(&sbus);
    for (int i = 0; i < 16; i++) {
        CHECK_EQ(sbus_raw(&sbus, i), 100 * i + 7);
    }
    CHECK_EQ(sbus_raw(&sbus, 16), 0); /* チャンネル番号が範囲外 */
}

static void test_skips_junk_before_header(void) {
    uint16_t ch[16];
    all_channels(ch, 1024);
    ch[3] = 1500;
    sbus_init(&sbus, &huart5);
    sbus.rxbuf[0] = 0xAA;
    sbus.rxbuf[1] = 0x00;
    sbus.rxbuf[2] = 0x55;
    receive_frame(ch, 0, 3);
    sbus_update(&sbus);
    CHECK_EQ(sbus_raw(&sbus, 3), 1500);
}

static void test_short_receive_is_ignored(void) {
    sbus_init(&sbus, &huart5);
    sbus.rxbuf[0] = 0x0F;
    HAL_UARTEx_RxEventCallback(&huart5, 10); /* 25 バイトに足りない */
    sbus_update(&sbus);
    CHECK_EQ(sbus_frame_count(&sbus), 0);
    CHECK(!sbus_ok(&sbus));
    CHECK_EQ(fake.uart_rx_start_calls, 2); /* 受信はかけ直す */
}

static void test_ok_judgement(void) {
    uint16_t ch[16];
    all_channels(ch, 1024);
    sbus_init(&sbus, &huart5);
    fake.tick = 5000;
    sbus_update(&sbus);
    CHECK(!sbus_ok(&sbus)); /* まだ 1 フレームも来ていない */

    receive_frame(ch, 0, 0);
    sbus_update(&sbus);
    CHECK(sbus_ok(&sbus));

    fake.tick = 5199;
    sbus_update(&sbus);
    CHECK(sbus_ok(&sbus)); /* 199ms ならまだ OK */
    fake.tick = 5200;
    sbus_update(&sbus);
    CHECK(!sbus_ok(&sbus)); /* 200ms 空いたら NG (コネクタ抜け) */

    receive_frame(ch, 0x08, 0); /* フェイルセーフ (送信機の電源 OFF) */
    sbus_update(&sbus);
    CHECK(!sbus_ok(&sbus));

    receive_frame(ch, 0x04, 0); /* フレーム落ちだけなら OK のまま (ノイズで立つ) */
    sbus_update(&sbus);
    CHECK(sbus_ok(&sbus));
    CHECK_EQ(fake.primask, 0);
}

static void test_values_change_only_on_update(void) {
    uint16_t ch[16];
    all_channels(ch, 1024);
    ch[0] = 1680;
    sbus_init(&sbus, &huart5);
    receive_frame(ch, 0, 0);
    CHECK_EQ(sbus_raw(&sbus, 0), 0); /* sbus_update を呼ぶまでは前の値 */
    sbus_update(&sbus);
    CHECK_EQ(sbus_raw(&sbus, 0), 1680);
}

static void test_stick_conversion(void) {
    uint16_t ch[16];
    all_channels(ch, 1024);
    ch[0] = 1680;
    ch[1] = 368;
    ch[2] = 1050;
    ch[3] = 1051;
    ch[4] = 998;
    ch[5] = 2000;
    ch[6] = 0;
    ch[7] = 1352;
    sbus_init(&sbus, &huart5);
    receive_frame(ch, 0, 0);
    sbus_update(&sbus);
    CHECK_EQ(sbus_stick(&sbus, 0), 1000);
    CHECK_EQ(sbus_stick(&sbus, 1), -1000);
    CHECK_EQ(sbus_stick(&sbus, 2), 0);  /* 中立 +26 までは 0 */
    CHECK_EQ(sbus_stick(&sbus, 3), 41); /* 27 * 1000 / 656 */
    CHECK_EQ(sbus_stick(&sbus, 4), 0);  /* 中立 -26 までは 0 */
    CHECK_EQ(sbus_stick(&sbus, 5), 1000); /* 範囲の外は頭打ち */
    CHECK_EQ(sbus_stick(&sbus, 6), -1000);
    CHECK_EQ(sbus_stick(&sbus, 7), 500); /* 328 * 1000 / 656 */
    CHECK_EQ(sbus_stick(&sbus, 16), 0);  /* チャンネル番号が範囲外 */
}

static void test_stick_asymmetric_range(void) {
    uint16_t ch[16];
    all_channels(ch, 1000);
    ch[0] = 1811;
    ch[1] = 172;
    ch[2] = 1405;
    sbus_init(&sbus, &huart5);
    sbus_set_stick_range(&sbus, 172, 1000, 1811); /* 中立の上下で幅が違う送信機 */
    sbus_set_deadband(&sbus, 0);
    receive_frame(ch, 0, 0);
    sbus_update(&sbus);
    CHECK_EQ(sbus_stick(&sbus, 0), 1000);
    CHECK_EQ(sbus_stick(&sbus, 1), -1000);
    CHECK_EQ(sbus_stick(&sbus, 3), 0);   /* 中立はちょうど 0 */
    CHECK_EQ(sbus_stick(&sbus, 2), 499); /* 405 * 1000 / 811 */
}

static void test_switches(void) {
    uint16_t ch[16];
    all_channels(ch, 1024);
    ch[0] = 1680;
    ch[1] = 368;
    ch[2] = 1353;
    ch[3] = 1352;
    ch[4] = 695;
    ch[5] = 696;
    sbus_init(&sbus, &huart5);
    receive_frame(ch, 0, 0);
    sbus_update(&sbus);
    CHECK_EQ(sbus_switch3(&sbus, 0), 1); /* 生の値が大きいほうが +1 */
    CHECK_EQ(sbus_switch3(&sbus, 1), -1);
    CHECK_EQ(sbus_switch3(&sbus, 6), 0); /* 中立 */
    CHECK_EQ(sbus_switch3(&sbus, 2), 1); /* 上の境目は (1024 + 1680) / 2 = 1352 */
    CHECK_EQ(sbus_switch3(&sbus, 3), 0);
    CHECK_EQ(sbus_switch3(&sbus, 4), -1); /* 下の境目は (368 + 1024) / 2 = 696 */
    CHECK_EQ(sbus_switch3(&sbus, 5), 0);
    CHECK_EQ(sbus_switch2(&sbus, 0), 1);
    CHECK_EQ(sbus_switch2(&sbus, 1), 0);
    CHECK_EQ(sbus_switch2(&sbus, 6), 0); /* 中立ちょうどは 0 */
}

static void test_values_are_zero_when_not_ok(void) {
    uint16_t ch[16];
    all_channels(ch, 1680);
    sbus_init(&sbus, &huart5);
    receive_frame(ch, 0x08, 0); /* フェイルセーフ */
    sbus_update(&sbus);
    CHECK(!sbus_ok(&sbus));
    CHECK_EQ(sbus_stick(&sbus, 0), 0);
    CHECK_EQ(sbus_switch2(&sbus, 0), 0);
    CHECK_EQ(sbus_switch3(&sbus, 0), 0);
    CHECK_EQ(sbus_raw(&sbus, 0), 1680); /* 生の値はそのまま */
}

static int hook_events, hook_errors;

/* ライブラリの weak な hook をテスト側で上書きして、呼ばれたことを数える */
void uart_rx_event_hook(UART_HandleTypeDef *huart, uint16_t size) {
    (void)huart;
    (void)size;
    hook_events++;
}

void uart_error_hook(UART_HandleTypeDef *huart) {
    (void)huart;
    hook_errors++;
}

static void test_error_restarts_rx(void) {
    sbus_init(&sbus, &huart5);
    HAL_UART_ErrorCallback(&huart5);
    CHECK_EQ(fake.uart_rx_start_calls, 2);
    CHECK_EQ(sbus_error_count(&sbus), 1);
}

static void test_other_uart_goes_to_hooks(void) {
    hook_events = hook_errors = 0;
    sbus_init(&sbus, &huart5);
    HAL_UARTEx_RxEventCallback(&huart_other, 10);
    HAL_UART_ErrorCallback(&huart_other);
    CHECK_EQ(hook_events, 1);
    CHECK_EQ(hook_errors, 1);
    CHECK_EQ(fake.uart_rx_start_calls, 1); /* SBUS のほうは再開しない */
}

static void test_bad_settings_are_rejected(void) {
    sbus_init(&sbus, &huart5);
    sbus_set_stick_range(&sbus, 1000, 500, 1600);
    CHECK_EQ(fake.last_error, LIB_ERR_PARAM);
    CHECK_EQ(sbus.stick_center, 1024); /* 変わっていない */
    sbus_set_deadband(&sbus, -1);
    CHECK_EQ(sbus.deadband, 26);
    CHECK_EQ(sbus_init(NULL, &huart5), LIB_ERR_PARAM);
    sbus_update(NULL);
    CHECK(!sbus_ok(NULL));
    CHECK_EQ(sbus_stick(NULL, 0), 0);
    CHECK_EQ(sbus_raw(NULL, 0), 0);
    CHECK_EQ(sbus_frame_count(NULL), 0);
}

int main(void) {
    RUN(test_init_starts_dma);
    RUN(test_init_without_dma_fails);
    RUN(test_init_reports_hal_error);
    RUN(test_decode_hand_made_frame);
    RUN(test_decode_all_channels);
    RUN(test_skips_junk_before_header);
    RUN(test_short_receive_is_ignored);
    RUN(test_ok_judgement);
    RUN(test_values_change_only_on_update);
    RUN(test_stick_conversion);
    RUN(test_stick_asymmetric_range);
    RUN(test_switches);
    RUN(test_values_are_zero_when_not_ok);
    RUN(test_error_restarts_rx);
    RUN(test_other_uart_goes_to_hooks);
    RUN(test_bad_settings_are_rejected);
    return TEST_RESULT();
}
