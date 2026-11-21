#include <string.h>

#include "dinosaur_robomas.h"
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

/* 1ms 進めてから robomas_update を呼ぶ (update は 1ms に 1 回だけ計算して送る)。前の 1ms に送った分は送り終わったことにする */
static lib_status_t update_next_ms(void) {
    fake.tick++;
    fake.can_free_mailboxes = 3;
    return robomas_update(&rm);
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
    update_next_ms();
    CHECK_EQ(sent_current(0, 1), 568); /* 誤差 360、積分 360 → 1.5 * 360 + 0.08 * 360 = 568.8 */
    feedback(1, 0, 300, 0, 0);         /* ローター 300rpm まで上がった */
    update_next_ms();
    CHECK_EQ(sent_current(1, 1), 123); /* 誤差 60、積分 420 → 1.5 * 60 + 0.08 * 420 = 123.6 */
}

static void test_integral_is_limited(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    feedback(1, 0, 0, 0, 0);
    robomas_set_speed_pid(m, 0.0f, 1.0f, 0.0f); /* 積分だけ */
    robomas_set_rpm(m, 100.0f);                     /* ローター 3600rpm。誤差 3600 */
    update_next_ms();
    CHECK_EQ(sent_current(0, 1), 3600);
    update_next_ms();
    CHECK_EQ(sent_current(1, 1), 5000); /* 積分 7200 → 5000 で頭打ち */
}

static void test_angle_mode(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_pid(m, 1.0f, 0.0f, 0.0f); /* P だけ */
    robomas_set_angle_pid(m, 2.0f, 0.0f, 0.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_angle(m, 10.0f); /* 誤差 10 度 → 目標 20rpm → 電流 20 */
    update_next_ms();
    CHECK_EQ(sent_current(0, 1), 20);
    robomas_set_angle(m, 360.0f); /* 誤差 360 度 → 720rpm だが 100rpm で頭打ち */
    update_next_ms();
    CHECK_EQ(sent_current(1, 1), 100);
    robomas_set_angle(m, -360.0f);
    update_next_ms();
    CHECK_EQ(sent_current(2, 1), -100);
}

static void test_mode_change_resets_integral(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_speed_pid(m, 0.0f, 1.0f, 0.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 10.0f); /* 誤差 360 */
    update_next_ms();          /* 積分 360 */
    robomas_set_current(m, 0);
    robomas_set_rpm(m, 10.0f);
    update_next_ms();
    CHECK_EQ(sent_current(1, 1), 360); /* 積分が 0 からやり直しなので 720 にはならない */
}

static void test_lost_feedback_outputs_zero_and_resets_integral(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_speed_pid(m, 0.0f, 1.0f, 0.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 10.0f);
    update_next_ms(); /* 積分 360 */
    fake.tick = 100;  /* フィードバックが 100ms 途絶えた */
    robomas_update(&rm);
    CHECK_EQ(sent_current(1, 1), 0);
    feedback(1, 0, 0, 0, 0); /* 復帰 */
    update_next_ms();
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

/* 指令の書き込みは割り込みを止めて行う (robomas_update が割り込みで動いても、途中の状態を読まれない) */
static void test_commands_lock_irq(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M3508, 1);
    int before = fake.irq_disable_calls;
    robomas_set_current(m, 100);
    CHECK(fake.irq_disable_calls > before);
    CHECK_EQ(fake.primask, 0);
    before = fake.irq_disable_calls;
    robomas_set_rpm(m, 10.0f);
    CHECK(fake.irq_disable_calls > before);
    CHECK_EQ(fake.primask, 0);
    before = fake.irq_disable_calls;
    robomas_set_angle(m, 10.0f);
    CHECK(fake.irq_disable_calls > before);
    CHECK_EQ(fake.primask, 0);
    before = fake.irq_disable_calls;
    robomas_stop_all(&rm);
    CHECK(fake.irq_disable_calls > before);
    CHECK_EQ(fake.primask, 0);
    before = fake.irq_disable_calls;
    robomas_set_speed_i_limit(m, 1000.0f);
    CHECK(fake.irq_disable_calls > before);
    before = fake.irq_disable_calls;
    robomas_set_angle_max_rpm(m, 50.0f);
    CHECK(fake.irq_disable_calls > before);
    before = fake.irq_disable_calls;
    robomas_set_gear_ratio(m, 10.0f);
    CHECK(fake.irq_disable_calls > before);
    before = fake.irq_disable_calls;
    robomas_set_current_limit(m, 1000);
    CHECK(fake.irq_disable_calls > before);
    before = fake.irq_disable_calls;
    robomas_set_accel_limit(m, 100.0f);
    CHECK(fake.irq_disable_calls > before);
    before = fake.irq_disable_calls;
    robomas_set_speed_pid(m, 1.0f, 0.1f, 0.0f);
    CHECK(fake.irq_disable_calls > before);
    before = fake.irq_disable_calls;
    robomas_set_angle_pid(m, 1.0f, 0.0f, 0.0f);
    CHECK(fake.irq_disable_calls > before);
    CHECK_EQ(fake.primask, 0);
}

/* robomas_update は全モーターの計算を割り込みを止めて行い、送る前に元に戻す */
static void test_update_locks_irq_and_restores(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M3508, 1);
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 10.0f);
    uint8_t zero[8] = {0};
    int before = fake.irq_disable_calls;
    can_send(&can1, 0x200, zero, 8); /* can_send 単独で止める回数 */
    int send_only = fake.irq_disable_calls - before;
    before = fake.irq_disable_calls;
    robomas_update(&rm);
    CHECK(fake.irq_disable_calls - before > send_only); /* 計算の分の止めが加わっている */
    CHECK_EQ(fake.primask, 0);
    CHECK_EQ(fake.can_sent_count, 2);
}

/*
 * while (1) の中で 1ms を待たずに呼んでも、計算して送るのは 1ms に 1 回だけ。
 * 毎回送ると、ID の小さい指令 (0x200) が CAN を埋めて ESC のフィードバック (0x201〜) が通れなくなり、全部止まってしまう。
 */
static void test_update_runs_at_most_once_per_ms(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_speed_pid(m, 0.0f, 1.0f, 0.0f); /* 積分だけ */
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 10.0f); /* 誤差 360 */
    fake.tick = 5;
    for (int i = 0; i < 10; i++) {
        CHECK_EQ(robomas_update(&rm), LIB_OK);
    }
    CHECK_EQ(fake.can_sent_count, 1);
    CHECK_EQ(sent_current(0, 1), 360); /* 積分は 1 回分だけ */
    fake.tick = 6;
    robomas_update(&rm);
    CHECK_EQ(fake.can_sent_count, 2);
    CHECK_EQ(sent_current(1, 1), 720);
    CHECK_EQ(fake.primask, 0);
}

/* robomas_init の前に robomas_add すると、init で消えてしまう。黙って消えないよう NULL を返す */
static void test_add_before_init_fails(void) {
    static robomas_t not_inited;
    CHECK(robomas_add(&not_inited, ROBOMAS_M3508, 1) == NULL);
    CHECK_EQ(fake.last_error, LIB_ERR_PARAM);
}

/* can_init の前に robomas_init すると、受信の登録が can_init で消える。失敗を返し、そのあとの add も NULL */
static void test_init_before_can_init_fails(void) {
    static can_t not_inited;
    static robomas_t r;
    CHECK(robomas_init(&r, &not_inited) != LIB_OK);
    CHECK(robomas_add(&r, ROBOMAS_M3508, 1) == NULL);
}

/* 電流の上限: どの種類の指令でも、送る電流をこの値で止める (初めて回すとき・機構を壊したくないとき用) */
static void test_current_limit_clamps_all_modes(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M3508, 1);
    feedback(1, 0, 0, 0, 0);
    robomas_set_current_limit(m, 3000);
    robomas_set_current(m, 5000);
    update_next_ms();
    CHECK_EQ(sent_current(0, 1), 3000);
    robomas_set_current(m, -5000);
    update_next_ms();
    CHECK_EQ(sent_current(1, 1), -3000);
    robomas_set_rpm(m, 300.0f); /* 速度制御の出力も止める */
    update_next_ms();
    CHECK_EQ(sent_current(2, 1), 3000);
    robomas_set_current_limit(m, 16384); /* 種類の最大までは戻せる */
    robomas_set_current(m, 5000);
    update_next_ms();
    CHECK_EQ(sent_current(3, 1), 5000);
}

static void test_current_limit_rejects_bad_values(void) {
    setup();
    robomas_motor_t *a = robomas_add(&rm, ROBOMAS_M3508, 1);
    robomas_motor_t *b = robomas_add(&rm, ROBOMAS_M2006, 2);
    feedback(1, 0, 0, 0, 0);
    feedback(2, 0, 0, 0, 0);
    robomas_set_current_limit(a, 2000);
    robomas_set_current_limit(b, 2000);
    robomas_set_current_limit(a, 16385); /* M3508 の最大 (16384) より大きい */
    CHECK_EQ(fake.last_error, LIB_ERR_PARAM);
    CHECK(strcmp(fake.last_where, "robomas_set_current_limit") == 0);
    robomas_set_current_limit(b, 10001); /* M2006 の最大 (10000) より大きい */
    robomas_set_current_limit(a, -1);
    CHECK_EQ(fake.error_count, 3);
    robomas_set_current(a, 5000);
    robomas_set_current(b, 5000);
    update_next_ms();
    CHECK_EQ(sent_current(0, 1), 2000); /* どれも変わっていない */
    CHECK_EQ(sent_current(0, 2), 2000);
    robomas_set_current_limit(NULL, 100);
}

/* 加速度の上限: 目標の rpm に、1ms ごとに (上限 / 1000) rpm ずつ近づける */
static void test_accel_limit_ramps_target(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_pid(m, 1.0f, 0.0f, 0.0f); /* P だけ。出力 = 目標 - 今の rpm */
    robomas_set_accel_limit(m, 1000.0f);            /* 1 秒で 1000rpm = 1ms で 1rpm */
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 3.0f);
    update_next_ms();
    CHECK_EQ(sent_current(0, 1), 1);
    update_next_ms();
    CHECK_EQ(sent_current(1, 1), 2);
    update_next_ms();
    CHECK_EQ(sent_current(2, 1), 3);
    update_next_ms();
    CHECK_EQ(sent_current(3, 1), 3); /* 目標に着いたら、そのまま */
    robomas_set_rpm(m, -3.0f);       /* 逆向きも同じ速さで変わる */
    update_next_ms();
    CHECK_EQ(sent_current(4, 1), 2);
}

/* 回っている途中で速度制御に切り替えたら、今の回転数から近づけ始める (0 rpm に急ブレーキしない) */
static void test_accel_limit_starts_from_measured_speed(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_pid(m, 1.0f, 0.0f, 0.0f);
    robomas_set_accel_limit(m, 1000.0f);
    feedback(1, 0, 500, 0, 0); /* 500rpm で回っている */
    robomas_set_current(m, 800);
    update_next_ms();
    robomas_set_rpm(m, 600.0f);
    update_next_ms();
    CHECK_EQ(sent_current(1, 1), 1); /* 目標は 501rpm (500 + 1)。出力 = 501 - 500 */
}

/* フィードバックが途絶えて戻ったら、そのときの回転数から近づけ直す */
static void test_accel_limit_restarts_after_lost_feedback(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_pid(m, 1.0f, 0.0f, 0.0f);
    robomas_set_accel_limit(m, 1000.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 100.0f);
    for (int i = 0; i < 50; i++) {
        update_next_ms(); /* 目標は 50rpm まで上がった */
    }
    fake.tick += 200; /* フィードバックが途絶えた */
    robomas_update(&rm);
    CHECK_EQ(sent_current(50, 1), 0);
    feedback(1, 0, 20, 0, 0); /* 20rpm で戻ってきた */
    update_next_ms();
    CHECK_EQ(sent_current(51, 1), 1); /* 目標は 21rpm (20 + 1) から */
}

/* robomas_stop_all は非常停止なので、加速度の上限に関係なくすぐ 0 */
static void test_stop_all_ignores_accel_limit(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_pid(m, 1.0f, 0.0f, 0.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 300.0f);
    update_next_ms();
    CHECK_EQ(sent_current(0, 1), 300);
    robomas_set_accel_limit(m, 1.0f); /* とてもゆっくり。もし止めるのにも効いたら、300 のまま */
    robomas_stop_all(&rm);
    update_next_ms();
    CHECK_EQ(sent_current(1, 1), 0);
}

/* 加速度の上限は速度制御 (robomas_set_rpm) だけに効く。位置制御は今までどおり (最高 rpm で頭打ち) */
static void test_accel_limit_does_not_change_angle_mode(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_pid(m, 1.0f, 0.0f, 0.0f);
    robomas_set_angle_pid(m, 2.0f, 0.0f, 0.0f);
    robomas_set_accel_limit(m, 1000.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_angle(m, 10.0f);
    update_next_ms();
    CHECK_EQ(sent_current(0, 1), 20);
}

static void test_accel_limit_rejects_bad_values(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_pid(m, 1.0f, 0.0f, 0.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_accel_limit(m, -1.0f);
    CHECK_EQ(fake.last_error, LIB_ERR_PARAM);
    CHECK(strcmp(fake.last_where, "robomas_set_accel_limit") == 0);
    robomas_set_accel_limit(NULL, 100.0f);
    robomas_set_rpm(m, 50.0f);
    update_next_ms();
    CHECK_EQ(sent_current(0, 1), 50); /* 初期値は上限なし (すぐ目標) のまま */
}

/* 速度の D: 今の回転数の変化 (1ms あたり) に kd を掛けて、変化を打ち消す向きに出す */
static void test_speed_kd(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_pid(m, 0.0f, 0.0f, 1.0f); /* D だけ */
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 10.0f);
    update_next_ms();
    CHECK_EQ(sent_current(0, 1), 0); /* 始めた瞬間は変化 0 */
    feedback(1, 0, 5, 0, 0);         /* 1ms で 5rpm 上がった */
    update_next_ms();
    CHECK_EQ(sent_current(1, 1), -5);
    update_next_ms();
    CHECK_EQ(sent_current(2, 1), 0); /* 変化しなければ 0 */
    robomas_set_rpm(m, 1000.0f);     /* 目標を急に変えても、D は飛び出さない */
    update_next_ms();
    CHECK_EQ(sent_current(3, 1), 0);
}

/* 積分の上限は robomas_set_speed_i_limit で決める。robomas_set_speed_pid は kp・ki・kd だけ変える (上限はそのまま) */
static void test_speed_pid_keeps_i_limit(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_i_limit(m, 100.0f);
    robomas_set_speed_pid(m, 0.0f, 1.0f, 0.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 3600.0f);
    update_next_ms();
    CHECK_EQ(sent_current(0, 1), 100); /* 積分は 100 で頭打ちのまま */
}

/* robomas_set_speed_i_limit は積分の上限だけ変える (ゲインはそのまま) */
static void test_speed_i_limit_keeps_gains(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_pid(m, 0.0f, 0.0f, 1.0f);
    robomas_set_speed_i_limit(m, 5000.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 10.0f);
    update_next_ms();
    feedback(1, 0, 5, 0, 0);
    update_next_ms();
    CHECK_EQ(sent_current(1, 1), -5);
}

/* 角度の I: 角度の差を足していき、目標 rpm に加える。I の分は最高 rpm を超えない */
static void test_angle_ki(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_pid(m, 1.0f, 0.0f, 0.0f); /* 速度は P = 1 だけ。出力 = 目標 rpm */
    robomas_set_angle_max_rpm(m, 15.0f);            /* 最高 15rpm */
    robomas_set_angle_pid(m, 0.0f, 1.0f, 0.0f);     /* 角度は I だけ */
    feedback(1, 0, 0, 0, 0);
    robomas_set_angle(m, 10.0f); /* 差 10 度 */
    update_next_ms();
    CHECK_EQ(sent_current(0, 1), 10);
    update_next_ms();
    CHECK_EQ(sent_current(1, 1), 15); /* 20 になるが、最高 15rpm で頭打ち */
    update_next_ms();
    CHECK_EQ(sent_current(2, 1), 15);
    robomas_set_angle(m, -10.0f); /* I は 15 で止まっていたので、15 - 10 = 5 */
    update_next_ms();
    CHECK_EQ(sent_current(3, 1), 5);
}

/* 角度の D: 今の角度の変化 (1ms あたり) に kd を掛けて、変化を打ち消す向きに出す */
static void test_angle_kd(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_pid(m, 1.0f, 0.0f, 0.0f);
    robomas_set_angle_max_rpm(m, 1000.0f);
    robomas_set_angle_pid(m, 0.0f, 0.0f, 1.0f); /* 角度は D だけ */
    feedback(1, 0, 0, 0, 0);
    robomas_set_angle(m, 0.0f);
    update_next_ms();
    CHECK_EQ(sent_current(0, 1), 0);
    feedback(1, 2048, 0, 0, 0); /* 1ms で 90 度進んだ */
    update_next_ms();
    CHECK_EQ(sent_current(1, 1), -90);
    update_next_ms();
    CHECK_EQ(sent_current(2, 1), 0);
    robomas_reset_angle(m); /* 原点を取り直しても、D は飛び出さない */
    update_next_ms();
    CHECK_EQ(sent_current(3, 1), 0);
}

/* 種類を変えたら角度の I も捨てる */
static void test_mode_change_resets_angle_i(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_pid(m, 1.0f, 0.0f, 0.0f);
    robomas_set_angle_max_rpm(m, 100.0f);
    robomas_set_angle_pid(m, 0.0f, 1.0f, 0.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_angle(m, 10.0f);
    update_next_ms(); /* I は 10 */
    robomas_set_current(m, 0);
    robomas_set_angle(m, 10.0f);
    update_next_ms();
    CHECK_EQ(sent_current(1, 1), 10); /* 0 からやり直しなので 20 にはならない */
}

static void test_pid_rejects_bad_values(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_pid(m, 1.0f, 0.0f, 0.0f);
    robomas_set_speed_pid(m, -1.0f, 0.0f, 0.0f); /* マイナスは間違い (逆に効いて暴れる) */
    CHECK_EQ(fake.last_error, LIB_ERR_PARAM);
    CHECK(strcmp(fake.last_where, "robomas_set_speed_pid") == 0);
    robomas_set_speed_pid(m, 1.0f, -1.0f, 0.0f);
    robomas_set_speed_pid(m, 1.0f, 0.0f, -1.0f);
    robomas_set_angle_pid(m, -1.0f, 0.0f, 0.0f);
    CHECK(strcmp(fake.last_where, "robomas_set_angle_pid") == 0);
    robomas_set_angle_pid(m, 1.0f, -1.0f, 0.0f);
    robomas_set_angle_pid(m, 1.0f, 0.0f, -1.0f);
    CHECK_EQ(fake.error_count, 6);
    robomas_set_speed_pid(NULL, 1.0f, 0.0f, 0.0f);
    robomas_set_angle_pid(NULL, 1.0f, 0.0f, 0.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 10.0f);
    update_next_ms();
    CHECK_EQ(sent_current(0, 1), 10); /* 変わっていない (kp 1) */
}

/* 角度制御のときの最高 rpm は robomas_set_angle_max_rpm で決める (ゲインはそのまま) */
static void test_angle_max_rpm(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_pid(m, 1.0f, 0.0f, 0.0f);
    robomas_set_angle_max_rpm(m, 50.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_angle(m, 360.0f); /* 2.0 × 360 = 720rpm だが 50rpm で頭打ち */
    update_next_ms();
    CHECK_EQ(sent_current(0, 1), 50);
    robomas_set_angle(m, 10.0f); /* kp は初期値の 2.0 のまま: 20rpm */
    update_next_ms();
    CHECK_EQ(sent_current(1, 1), 20);
}

static void test_limits_reject_bad_values(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_i_limit(m, -1.0f);
    CHECK_EQ(fake.last_error, LIB_ERR_PARAM);
    CHECK(strcmp(fake.last_where, "robomas_set_speed_i_limit") == 0);
    robomas_set_angle_max_rpm(m, -1.0f);
    CHECK(strcmp(fake.last_where, "robomas_set_angle_max_rpm") == 0);
    robomas_set_speed_i_limit(m, NAN);
    robomas_set_angle_max_rpm(m, NAN);
    CHECK_EQ(fake.error_count, 4);
    robomas_set_speed_i_limit(NULL, 100.0f);
    robomas_set_angle_max_rpm(NULL, 100.0f);
    robomas_set_speed_pid(m, 1.0f, 0.0f, 0.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_angle(m, 360.0f);
    update_next_ms();
    CHECK_EQ(sent_current(0, 1), 100); /* 最高 rpm は初期値の 100 のまま */
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
    RUN(test_commands_lock_irq);
    RUN(test_update_locks_irq_and_restores);
    RUN(test_update_runs_at_most_once_per_ms);
    RUN(test_add_before_init_fails);
    RUN(test_init_before_can_init_fails);
    RUN(test_current_limit_clamps_all_modes);
    RUN(test_current_limit_rejects_bad_values);
    RUN(test_accel_limit_ramps_target);
    RUN(test_accel_limit_starts_from_measured_speed);
    RUN(test_accel_limit_restarts_after_lost_feedback);
    RUN(test_stop_all_ignores_accel_limit);
    RUN(test_accel_limit_does_not_change_angle_mode);
    RUN(test_accel_limit_rejects_bad_values);
    RUN(test_speed_kd);
    RUN(test_speed_pid_keeps_i_limit);
    RUN(test_speed_i_limit_keeps_gains);
    RUN(test_angle_ki);
    RUN(test_angle_kd);
    RUN(test_mode_change_resets_angle_i);
    RUN(test_pid_rejects_bad_values);
    RUN(test_angle_max_rpm);
    RUN(test_limits_reject_bad_values);
    RUN(test_bad_settings_and_null);
    return TEST_RESULT();
}
