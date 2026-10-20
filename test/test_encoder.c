#include "dinosaur_encoder.h"
#include "test.h"

static TIM_TypeDef tim16 = {.CNT = 0, .ARR = 0xFFFF};
static TIM_TypeDef tim32 = {.CNT = 0, .ARR = 0xFFFFFFFF};
static TIM_HandleTypeDef htim16 = {.Instance = &tim16};
static TIM_HandleTypeDef htim32 = {.Instance = &tim32};
static encoder_t enc;

static void test_init(void) {
    tim16.CNT = 500;
    CHECK_EQ(encoder_init(&enc, &htim16, 2048), LIB_OK);
    CHECK_EQ(fake.tim_start_calls, 1);
    CHECK(fake_coredebug.DEMCR & CoreDebug_DEMCR_TRCENA_Msk); /* DWT のサイクルカウンタを動かしている */
    CHECK(fake_dwt.CTRL & DWT_CTRL_CYCCNTENA_Msk);
    CHECK_EQ(encoder_count(&enc), 0); /* 起動時の位置が 0 */
    CHECK_EQ(encoder_init(&enc, &htim16, 0), LIB_ERR_PARAM);
    CHECK_EQ(encoder_init(&enc, NULL, 2048), LIB_ERR_PARAM);
    fake.tim_start_ret = HAL_ERROR;
    CHECK_EQ(encoder_init(&enc, &htim16, 2048), LIB_ERR_HAL);
    CHECK_EQ(fake.last_error, LIB_ERR_HAL);
}

static void test_counts_and_delta(void) {
    tim16.CNT = 1000;
    encoder_init(&enc, &htim16, 2048);
    tim16.CNT = 1100;
    encoder_update(&enc);
    CHECK_EQ(encoder_delta(&enc), 100);
    CHECK_EQ(encoder_count(&enc), 100);
    tim16.CNT = 1050;
    encoder_update(&enc);
    CHECK_EQ(encoder_delta(&enc), -50);
    CHECK_EQ(encoder_count(&enc), 50);
}

/* カウントが int32 の端を越えても、符号付きの桁あふれ (未定義動作) にならずに回り込む */
static void test_count_wraps_without_overflow(void) {
    tim16.CNT = 100;
    encoder_init(&enc, &htim16, 2048);
    enc.count = INT32_MAX;
    tim16.CNT = 101;
    encoder_update(&enc);
    CHECK_EQ(encoder_count(&enc), INT32_MIN);
}

static void test_16bit_wrap(void) {
    tim16.CNT = 65530;
    encoder_init(&enc, &htim16, 2048);
    tim16.CNT = 10; /* 65535 → 0 をまたいで +16 */
    encoder_update(&enc);
    CHECK_EQ(encoder_delta(&enc), 16);
    tim16.CNT = 65530; /* 逆向きにまたいで -16 */
    encoder_update(&enc);
    CHECK_EQ(encoder_delta(&enc), -16);
    CHECK_EQ(encoder_count(&enc), 0);
}

static void test_32bit_wrap(void) {
    tim32.CNT = 0xFFFFFFF0u;
    encoder_init(&enc, &htim32, 2048);
    tim32.CNT = 0x10;
    encoder_update(&enc);
    CHECK_EQ(encoder_delta(&enc), 32);
    tim32.CNT = 0x7FFF0000u; /* 32bit なら、16bit の半周を超えて進んでも正しく数える */
    encoder_update(&enc);
    CHECK_EQ(encoder_delta(&enc), 0x7FFF0000 - 0x10);
}

static void test_reverse(void) {
    tim16.CNT = 0;
    encoder_init(&enc, &htim16, 2048);
    encoder_set_reverse(&enc, true);
    tim16.CNT = 300;
    encoder_update(&enc);
    CHECK_EQ(encoder_delta(&enc), -300);
    CHECK_EQ(encoder_count(&enc), -300);
}

static void test_rpm_and_turns(void) {
    tim16.CNT = 0;
    fake_dwt.CYCCNT = 0;
    encoder_init(&enc, &htim16, 1000); /* SystemCoreClock は 100MHz (fake_reset) */
    tim16.CNT = 100;
    fake_dwt.CYCCNT = 1000000; /* 10ms 後に 0.1 回転 = 10 回転/秒 = 600rpm */
    fake.tick = 10;
    encoder_update(&enc);
    CHECK_NEAR(encoder_rpm(&enc), 600.0, 0.01);
    CHECK_NEAR(encoder_turns(&enc), 0.1, 1e-6);
    tim16.CNT = 50; /* 5ms 後に 0.05 回転戻った = -600rpm */
    fake_dwt.CYCCNT = 1500000;
    fake.tick = 15;
    encoder_update(&enc);
    CHECK_NEAR(encoder_rpm(&enc), -600.0, 0.01);
}

static void test_rpm_across_cycle_counter_wrap(void) {
    tim16.CNT = 0;
    fake_dwt.CYCCNT = 0xFFFFFFFFu - 499999u; /* あと 500000 で一周する */
    encoder_init(&enc, &htim16, 1000);
    tim16.CNT = 100;
    fake_dwt.CYCCNT = 499999u; /* 一周またいで 1000000 サイクル (10ms) 後 */
    fake.tick = 10;
    encoder_update(&enc);
    CHECK_NEAR(encoder_rpm(&enc), 600.0, 0.01);
}

static void test_rpm_zero_after_long_gap(void) {
    tim16.CNT = 0;
    encoder_init(&enc, &htim16, 1000);
    tim16.CNT = 100;
    fake.tick = 10000; /* 10 秒空いた (DWT が一周しているかもしれない) */
    fake_dwt.CYCCNT = 12345;
    encoder_update(&enc);
    CHECK_NEAR(encoder_rpm(&enc), 0.0, 1e-9);
    CHECK_EQ(encoder_count(&enc), 100); /* カウントは数える */
}

static void test_reset(void) {
    tim16.CNT = 0;
    encoder_init(&enc, &htim16, 1000);
    tim16.CNT = 700;
    encoder_update(&enc);
    encoder_reset(&enc);
    CHECK_EQ(encoder_count(&enc), 0);
    tim16.CNT = 710;
    encoder_update(&enc);
    CHECK_EQ(encoder_count(&enc), 10);
}

static void test_null_is_safe(void) {
    encoder_update(NULL);
    encoder_reset(NULL);
    encoder_set_reverse(NULL, true);
    CHECK_EQ(encoder_count(NULL), 0);
    CHECK_EQ(encoder_delta(NULL), 0);
    CHECK_NEAR(encoder_rpm(NULL), 0.0, 1e-9);
    CHECK_NEAR(encoder_turns(NULL), 0.0, 1e-9);
    CHECK_EQ(encoder_init(NULL, &htim16, 1000), LIB_ERR_PARAM);
}

int main(void) {
    RUN(test_init);
    RUN(test_counts_and_delta);
    RUN(test_count_wraps_without_overflow);
    RUN(test_16bit_wrap);
    RUN(test_32bit_wrap);
    RUN(test_reverse);
    RUN(test_rpm_and_turns);
    RUN(test_rpm_across_cycle_counter_wrap);
    RUN(test_rpm_zero_after_long_gap);
    RUN(test_reset);
    RUN(test_null_is_safe);
    return TEST_RESULT();
}
