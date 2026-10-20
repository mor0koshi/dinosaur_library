#include "mor0koshi_robomas.h"
#include "test.h"

static CAN_HandleTypeDef hcan1 = {.Instance = CAN1};
static can_t can1;
static robomas_t rm;

/* ESC からのフィードバックを 1 フレーム受信させる (ビッグエンディアン) */
static void feedback(int esc_id, uint16_t angle, int16_t rpm, int16_t current, uint8_t temp) {
    uint8_t d[8] = {
        (uint8_t)(angle >> 8), (uint8_t)angle,
        (uint8_t)((uint16_t)rpm >> 8), (uint8_t)rpm,
        (uint8_t)((uint16_t)current >> 8), (uint8_t)current,
        temp, 0,
    };
    fake_can_receive(&hcan1, 0x200 + (uint32_t)esc_id, d, 8);
}

/* frame 番目に送った電流指令フレームから、ESC の ID の値を取り出す (ビッグエンディアン) */
static int16_t sent_current(int frame, int esc_id) {
    const uint8_t *d = fake.can_sent[frame].data;
    int slot = (esc_id - 1) % 4;
    return (int16_t)((d[slot * 2] << 8) | d[slot * 2 + 1]);
}

static void setup(void) {
    can_init(&can1, &hcan1);
    CHECK_EQ(robomas_init(&rm, &can1), LIB_OK);
}

static void test_add(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M3508, 1);
    CHECK(m != NULL);
    CHECK(robomas_add(&rm, ROBOMAS_M3508, 1) == NULL); /* 同じ ID は二重に登録できない */
    CHECK_EQ(fake.last_error, LIB_ERR_PARAM);
    CHECK(robomas_add(&rm, ROBOMAS_M2006, 0) == NULL);
    CHECK(robomas_add(&rm, ROBOMAS_M2006, 9) == NULL);
    CHECK(robomas_add(&rm, ROBOMAS_M2006, 8) != NULL);
}

static void test_update_without_motors_sends_nothing(void) {
    setup();
    CHECK_EQ(robomas_update(&rm), LIB_OK);
    CHECK_EQ(fake.can_sent_count, 0);
}

static void test_feedback_parse(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M3508, 2);
    CHECK(!robomas_ok(m));
    fake.tick = 100;
    feedback(2, 1000, -1920, -300, 45);
    CHECK(robomas_ok(m));
    CHECK_NEAR(robomas_rpm(m), -1920.0 / (3591.0 / 187.0), 1e-3); /* 出力軸 rpm */
    CHECK_EQ(robomas_current(m), -300);
    CHECK_EQ(robomas_temp(m), 45);
    CHECK_NEAR(robomas_angle(m), 0.0, 1e-9); /* 最初のフィードバックの位置が 0 度 */
    fake.tick = 199;
    CHECK(robomas_ok(m));
    fake.tick = 200;
    CHECK(!robomas_ok(m)); /* 100ms 来なければ NG */
}

static void test_feedback_for_unregistered_id_is_ignored(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M3508, 1);
    feedback(3, 1000, 500, 0, 30);
    CHECK(!robomas_ok(m));
}

static void test_m2006_temp_is_zero(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    feedback(1, 0, 0, 0, 99); /* C610 は温度を返さない */
    CHECK_EQ(robomas_temp(m), 0);
}

static void test_multi_turn_angle(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f); /* ローターの角度で確かめる */
    feedback(1, 8000, 0, 0, 0);      /* ここが 0 度 */
    feedback(1, 100, 0, 0, 0);       /* 8191 → 0 をまたいで +292 */
    CHECK_NEAR(robomas_angle(m), 292.0 * 360.0 / 8192.0, 1e-3);
    for (int i = 0; i < 10; i++) { /* 2048 ずつ 4 回 = 1 回転、を 10 回転分 */
        for (int k = 1; k <= 4; k++) {
            feedback(1, (uint16_t)((100 + 2048 * k) % 8192), 0, 0, 0);
        }
    }
    CHECK_NEAR(robomas_angle(m), 3600.0 + 292.0 * 360.0 / 8192.0, 1e-2);
    feedback(1, 8000, 0, 0, 0); /* 0 → 8191 を逆向きにまたいで -292 */
    CHECK_NEAR(robomas_angle(m), 3600.0, 1e-2);
}

static void test_angle_uses_gear_ratio_and_reset(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1); /* ギア比 36 */
    feedback(1, 0, 0, 0, 0);
    for (int k = 1; k <= 36 * 4; k++) { /* ローター 36 回転 */
        feedback(1, (uint16_t)((2048 * k) % 8192), 0, 0, 0);
    }
    CHECK_NEAR(robomas_angle(m), 360.0, 1e-2); /* 出力軸は 1 回転 */
    robomas_reset_angle(m);
    CHECK_NEAR(robomas_angle(m), 0.0, 1e-9);
    feedback(1, 2048, 0, 0, 0); /* ローター 1/4 回転 = 出力軸 2.5 度 */
    CHECK_NEAR(robomas_angle(m), 90.0 / 36.0, 1e-3);
    CHECK_EQ(fake.primask, 0);
}

static void test_current_mode_frames(void) {
    setup();
    robomas_motor_t *m1 = robomas_add(&rm, ROBOMAS_M3508, 1);
    robomas_motor_t *m3 = robomas_add(&rm, ROBOMAS_M3508, 3);
    feedback(1, 0, 0, 0, 0);
    feedback(3, 0, 0, 0, 0);
    robomas_set_current(m1, 1000);
    robomas_set_current(m3, -2000);
    CHECK_EQ(robomas_update(&rm), LIB_OK);
    CHECK_EQ(fake.can_sent_count, 1); /* ID 5〜8 がいないので 0x1FF は送らない */
    CHECK_EQ(fake.can_sent[0].header.StdId, 0x200);
    CHECK_EQ(fake.can_sent[0].header.DLC, 8);
    CHECK_EQ(sent_current(0, 1), 1000);
    CHECK_EQ(sent_current(0, 2), 0); /* 登録していない ID は 0 */
    CHECK_EQ(sent_current(0, 3), -2000);
    CHECK_EQ(fake.can_sent[0].data[0], 0x03); /* 1000 = 0x03E8。ビッグエンディアン */
    CHECK_EQ(fake.can_sent[0].data[1], 0xE8);
}

static void test_high_ids_use_0x1ff(void) {
    setup();
    robomas_motor_t *m6 = robomas_add(&rm, ROBOMAS_M2006, 6);
    feedback(6, 0, 0, 0, 0);
    robomas_set_current(m6, 500);
    robomas_update(&rm);
    CHECK_EQ(fake.can_sent_count, 1); /* ID 1〜4 がいないので 0x200 は送らない */
    CHECK_EQ(fake.can_sent[0].header.StdId, 0x1FF);
    CHECK_EQ(sent_current(0, 6), 500);
}

static void test_both_frames(void) {
    setup();
    robomas_motor_t *a = robomas_add(&rm, ROBOMAS_M3508, 1);
    robomas_motor_t *b = robomas_add(&rm, ROBOMAS_M2006, 5);
    feedback(1, 0, 0, 0, 0);
    feedback(5, 0, 0, 0, 0);
    robomas_set_current(a, 111);
    robomas_set_current(b, 222);
    robomas_update(&rm);
    CHECK_EQ(fake.can_sent_count, 2);
    CHECK_EQ(fake.can_sent[0].header.StdId, 0x200);
    CHECK_EQ(fake.can_sent[1].header.StdId, 0x1FF);
    CHECK_EQ(sent_current(0, 1), 111);
    CHECK_EQ(sent_current(1, 5), 222);
}

static void test_current_is_clamped(void) {
    setup();
    robomas_motor_t *a = robomas_add(&rm, ROBOMAS_M3508, 1);
    robomas_motor_t *b = robomas_add(&rm, ROBOMAS_M2006, 2);
    feedback(1, 0, 0, 0, 0);
    feedback(2, 0, 0, 0, 0);
    robomas_set_current(a, 30000);
    robomas_set_current(b, -30000);
    robomas_update(&rm);
    CHECK_EQ(sent_current(0, 1), 16384);  /* M3508 は ±16384 */
    CHECK_EQ(sent_current(0, 2), -10000); /* M2006 は ±10000 */
}

static void test_no_feedback_yet_outputs_zero(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M3508, 1);
    robomas_set_current(m, 1000);
    robomas_update(&rm);
    CHECK_EQ(sent_current(0, 1), 0);
}

static void test_speed_pi(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1); /* kp 1.5 / ki 0.08 / ギア比 36 */
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 10.0f); /* 出力軸 10rpm = ローター 360rpm */
    robomas_update(&rm);
    CHECK_EQ(sent_current(0, 1), 568); /* 誤差 360、積分 360 → 1.5 * 360 + 0.08 * 360 = 568.8 */
    feedback(1, 0, 300, 0, 0);         /* ローター 300rpm まで上がった */
    robomas_update(&rm);
    CHECK_EQ(sent_current(1, 1), 123); /* 誤差 60、積分 420 → 1.5 * 60 + 0.08 * 420 = 123.6 */
}

static void test_integral_is_limited(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    feedback(1, 0, 0, 0, 0);
    robomas_set_speed_gain(m, 0.0f, 1.0f, 5000.0f); /* 積分だけ */
    robomas_set_rpm(m, 100.0f);                     /* ローター 3600rpm。誤差 3600 */
    robomas_update(&rm);
    CHECK_EQ(sent_current(0, 1), 3600);
    robomas_update(&rm);
    CHECK_EQ(sent_current(1, 1), 5000); /* 積分 7200 → 5000 で頭打ち */
}

static void test_angle_mode(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_gain(m, 1.0f, 0.0f, 5000.0f); /* P だけ */
    robomas_set_angle_gain(m, 2.0f, 100.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_angle(m, 10.0f); /* 誤差 10 度 → 目標 20rpm → 電流 20 */
    robomas_update(&rm);
    CHECK_EQ(sent_current(0, 1), 20);
    robomas_set_angle(m, 360.0f); /* 誤差 360 度 → 720rpm だが 100rpm で頭打ち */
    robomas_update(&rm);
    CHECK_EQ(sent_current(1, 1), 100);
    robomas_set_angle(m, -360.0f);
    robomas_update(&rm);
    CHECK_EQ(sent_current(2, 1), -100);
}

static void test_mode_change_resets_integral(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_speed_gain(m, 0.0f, 1.0f, 5000.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 10.0f); /* 誤差 360 */
    robomas_update(&rm);       /* 積分 360 */
    robomas_set_current(m, 0);
    robomas_set_rpm(m, 10.0f);
    robomas_update(&rm);
    CHECK_EQ(sent_current(1, 1), 360); /* 積分が 0 からやり直しなので 720 にはならない */
}

static void test_lost_feedback_outputs_zero_and_resets_integral(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_speed_gain(m, 0.0f, 1.0f, 5000.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 10.0f);
    robomas_update(&rm); /* 積分 360 */
    fake.tick = 100;     /* フィードバックが 100ms 途絶えた */
    robomas_update(&rm);
    CHECK_EQ(sent_current(1, 1), 0);
    feedback(1, 0, 0, 0, 0); /* 復帰 */
    robomas_update(&rm);
    CHECK_EQ(sent_current(2, 1), 360); /* 積分はリセットされている (720 にならない) */
}

static void test_stop_all(void) {
    setup();
    robomas_motor_t *a = robomas_add(&rm, ROBOMAS_M3508, 1);
    robomas_motor_t *b = robomas_add(&rm, ROBOMAS_M2006, 5);
    feedback(1, 0, 0, 0, 0);
    feedback(5, 0, 0, 0, 0);
    robomas_set_rpm(a, 50.0f);
    robomas_set_current(b, 3000);
    robomas_stop_all(&rm);
    robomas_update(&rm);
    CHECK_EQ(sent_current(0, 1), 0);
    CHECK_EQ(sent_current(1, 5), 0);
}

static void test_bad_settings_and_null(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 0.0f);
    CHECK_EQ(fake.last_error, LIB_ERR_PARAM);
    feedback(1, 0, 360, 0, 0);
    CHECK_NEAR(robomas_rpm(m), 10.0, 1e-6); /* ギア比は 36 のまま */
    CHECK(robomas_add(NULL, ROBOMAS_M3508, 1) == NULL);
    robomas_set_rpm(NULL, 1.0f);
    robomas_stop_all(NULL);
    CHECK_NEAR(robomas_angle(NULL), 0.0, 1e-9);
    CHECK(!robomas_ok(NULL));
    CHECK_EQ(robomas_update(NULL), LIB_ERR_PARAM);
    CHECK_EQ(robomas_init(NULL, &can1), LIB_ERR_PARAM);
}

int main(void) {
    RUN(test_add);
    RUN(test_update_without_motors_sends_nothing);
    RUN(test_feedback_parse);
    RUN(test_feedback_for_unregistered_id_is_ignored);
    RUN(test_m2006_temp_is_zero);
    RUN(test_multi_turn_angle);
    RUN(test_angle_uses_gear_ratio_and_reset);
    RUN(test_current_mode_frames);
    RUN(test_high_ids_use_0x1ff);
    RUN(test_both_frames);
    RUN(test_current_is_clamped);
    RUN(test_no_feedback_yet_outputs_zero);
    RUN(test_speed_pi);
    RUN(test_integral_is_limited);
    RUN(test_angle_mode);
    RUN(test_mode_change_resets_integral);
    RUN(test_lost_feedback_outputs_zero_and_resets_integral);
    RUN(test_stop_all);
    RUN(test_bad_settings_and_null);
    return TEST_RESULT();
}
