/*
 * ロボマスモーター M3508 (C620) / M2006 (C610)。AIcontest の main.c を元にしている。
 *   指令: 0x200 (ESC の ID 1〜4)、0x1FF (ID 5〜8)。各 2 バイトの int16、ビッグエンディアン
 *   フィードバック: 0x200 + ID。[0..1] 角度 0〜8191、[2..3] ローター rpm、[4..5] 電流、[6] 温度 (C620 のみ)
 */
#include "dinosaur_robomas.h"

#include <string.h>

#include "dinosaur_port.h"

#if defined(HAL_CAN_MODULE_ENABLED)

#define ROBOMAS_FB_FIRST 0x201U
#define ROBOMAS_FB_LAST 0x208U
#define ROBOMAS_CMD_LOW 0x200U  /* ESC の ID 1〜4 への電流指令 */
#define ROBOMAS_CMD_HIGH 0x1FFU /* ESC の ID 5〜8 への電流指令 */
#define ROBOMAS_COUNTS_PER_REV 8192

/* 種類ごとの電流の最大 (ESC が受け付ける範囲) */
static int16_t max_current(robomas_type_t type) { return type == ROBOMAS_M3508 ? 16384 : 10000; }

static float clampf(float v, float lim) {
    if (v > lim) {
        return lim;
    }
    if (v < -lim) {
        return -lim;
    }
    return v;
}

/*
 * フィードバック (受信割り込みの中)。
 * 生の角度は 1 回転 (0〜8191) で一周するので、届くたびに前回との差を -4096〜+4095 に折り返して足す。
 * 1ms ごとに届き、最高回転 (約 9000rpm) でも 1ms で半回転未満なので数え間違えない。
 * robomas_update の周期とは関係なく正しく数えるために、ここ (割り込みの中) でやる。
 */
static void on_feedback(uint32_t id, const uint8_t *d, uint8_t len, void *arg) {
    robomas_t *rm = arg;
    robomas_motor_t *m = &rm->motors[id - ROBOMAS_FB_FIRST];
    if (!m->used || len < 7U) {
        return;
    }
    uint16_t raw = (uint16_t)((d[0] << 8) | d[1]);
    if (m->fb_started) {
        int32_t diff = (int32_t)raw - (int32_t)m->fb_raw_angle;
        if (diff > 4095) {
            diff -= ROBOMAS_COUNTS_PER_REV;
        } else if (diff < -4096) {
            diff += ROBOMAS_COUNTS_PER_REV;
        }
        m->fb_total += diff;
    }
    m->fb_raw_angle = raw;
    m->fb_rpm = (int16_t)((d[2] << 8) | d[3]);
    m->fb_current = (int16_t)((d[4] << 8) | d[5]);
    m->fb_temp = d[6];
    m->fb_ms = HAL_GetTick(); /* fb_started より先に書く */
    m->fb_started = true;
}

lib_status_t robomas_init(robomas_t *rm, can_t *bus) {
    /* bus->hcan が NULL = can_init がまだ。ここで受信を登録しても can_init で消える */
    if (rm == NULL || bus == NULL || bus->hcan == NULL) {
        return port_fail(LIB_ERR_PARAM, "robomas_init");
    }
    memset(rm, 0, sizeof *rm);
    lib_status_t st = can_subscribe_range(bus, ROBOMAS_FB_FIRST, ROBOMAS_FB_LAST, on_feedback, rm);
    if (st == LIB_OK) {
        rm->bus = bus; /* 失敗したら NULL のままにして、robomas_add も失敗させる (黙って動かないのを防ぐ) */
    }
    return st;
}

robomas_motor_t *robomas_add(robomas_t *rm, robomas_type_t type, int id) {
    /* rm->bus が NULL = robomas_init がまだ。ここで登録しても robomas_init で消える */
    if (rm == NULL || rm->bus == NULL || id < 1 || id > ROBOMAS_MAX_MOTORS ||
        (type != ROBOMAS_M3508 && type != ROBOMAS_M2006) || rm->motors[id - 1].used) {
        lib_error_hook(LIB_ERR_PARAM, "robomas_add");
        return NULL;
    }
    robomas_motor_t *m = &rm->motors[id - 1];
    uint32_t pm = port_irq_save();
    memset(m, 0, sizeof *m);
    m->type = type;
    m->id = (uint8_t)id;
    m->gear = type == ROBOMAS_M3508 ? 3591.0f / 187.0f : 36.0f;
    m->current_limit = max_current(type);
    /* M2006 の速度ゲインは AIcontest で確認済みの値。M3508 と位置制御は仮の値 (実機で調整する) */
    m->speed_kp = 1.5f;
    m->speed_ki = 0.08f;
    m->speed_i_limit = 5000.0f;
    m->angle_kp = 2.0f;
    m->angle_max_rpm = 100.0f;
    m->accel_limit = 0.0f; /* 上限なし */
    m->restart = true;     /* speed_kd・angle_ki・angle_kd は memset で 0 (今までどおり速度 PI・角度 P) */
    m->mode = ROBOMAS_MODE_CURRENT;
    m->target = 0.0f;
    m->used = true; /* 設定を書き終えてから使う印を立てる (割り込みが途中の状態を見ないように) */
    port_irq_restore(pm);
    return m;
}

/* 積分を捨て、加速度の上限の目標と D の前回の値を、次の計算で今の値から始め直す */
static void reset_control(robomas_motor_t *m) {
    m->integral = 0.0f;
    m->angle_iterm = 0.0f;
    m->restart = true;
}

/* 制御の種類が変わったら積分を捨てる (前の種類の積分が残ると、切り替えた瞬間に飛び出す) */
static void set_mode(robomas_motor_t *m, robomas_mode_t mode) {
    if (m->mode != mode) {
        m->mode = mode;
        reset_control(m);
    }
}

void robomas_set_current(robomas_motor_t *m, int16_t current) {
    if (m == NULL) {
        return;
    }
    /* 種類と目標を一緒に書く。間に robomas_update (タイマー割り込み) が入ると、前の目標が新しい種類で使われる */
    uint32_t pm = port_irq_save();
    set_mode(m, ROBOMAS_MODE_CURRENT);
    m->target = (float)current;
    port_irq_restore(pm);
}

void robomas_set_rpm(robomas_motor_t *m, float rpm) {
    if (m == NULL) {
        return;
    }
    uint32_t pm = port_irq_save();
    set_mode(m, ROBOMAS_MODE_SPEED);
    m->target = rpm;
    port_irq_restore(pm);
}

void robomas_set_angle(robomas_motor_t *m, float deg) {
    if (m == NULL) {
        return;
    }
    uint32_t pm = port_irq_save();
    set_mode(m, ROBOMAS_MODE_ANGLE);
    m->target = deg;
    port_irq_restore(pm);
}

bool robomas_ok(const robomas_motor_t *m) {
    if (m == NULL || !m->fb_started) {
        return false;
    }
    uint32_t fb_ms = m->fb_ms; /* 先に読む (後から HAL_GetTick を読めば、差が負にならない) */
    return HAL_GetTick() - fb_ms < ROBOMAS_TIMEOUT_MS;
}

float robomas_rpm(const robomas_motor_t *m) {
    if (m == NULL) {
        return 0.0f;
    }
    return (float)m->fb_rpm / m->gear;
}

/* 64bit の値は 1 命令で読めないので、割り込みを止めて読む */
static int64_t read_total(const robomas_motor_t *m) {
    uint32_t pm = port_irq_save();
    int64_t total = m->fb_total;
    port_irq_restore(pm);
    return total;
}

float robomas_angle(const robomas_motor_t *m) {
    if (m == NULL) {
        return 0.0f;
    }
    return (float)read_total(m) * (360.0f / (float)ROBOMAS_COUNTS_PER_REV) / m->gear;
}

void robomas_reset_angle(robomas_motor_t *m) {
    if (m == NULL) {
        return;
    }
    uint32_t pm = port_irq_save();
    m->fb_total = 0;
    m->prev_angle = 0.0f; /* 角度の D が、原点の取り直しを「急に動いた」と思わないように */
    port_irq_restore(pm);
}

int16_t robomas_current(const robomas_motor_t *m) { return m == NULL ? 0 : m->fb_current; }

int robomas_temp(const robomas_motor_t *m) {
    if (m == NULL || m->type == ROBOMAS_M2006) {
        return 0; /* C610 は温度を返さない */
    }
    return m->fb_temp;
}

/* 調整の関数も、robomas_update (タイマー割り込み) の計算の途中で値が変わらないよう、割り込みを止めて書く */
void robomas_set_gear_ratio(robomas_motor_t *m, float ratio) {
    if (m == NULL || !(ratio > 0.0f)) {
        lib_error_hook(LIB_ERR_PARAM, "robomas_set_gear_ratio");
        return;
    }
    uint32_t pm = port_irq_save();
    m->gear = ratio;
    port_irq_restore(pm);
}

void robomas_set_speed_i_limit(robomas_motor_t *m, float i_limit) {
    if (m == NULL || !(i_limit >= 0.0f)) { /* NaN も断る */
        lib_error_hook(LIB_ERR_PARAM, "robomas_set_speed_i_limit");
        return;
    }
    uint32_t pm = port_irq_save();
    m->speed_i_limit = i_limit;
    m->integral = clampf(m->integral, i_limit);
    port_irq_restore(pm);
}

/* PID のゲインは 0 以上 (マイナスだと逆に効いて暴れる)。NaN も断る */
static bool valid_gains(float kp, float ki, float kd) { return kp >= 0.0f && ki >= 0.0f && kd >= 0.0f; }

void robomas_set_speed_pid(robomas_motor_t *m, float kp, float ki, float kd) {
    if (m == NULL || !valid_gains(kp, ki, kd)) {
        lib_error_hook(LIB_ERR_PARAM, "robomas_set_speed_pid");
        return;
    }
    uint32_t pm = port_irq_save();
    m->speed_kp = kp;
    m->speed_ki = ki;
    m->speed_kd = kd;
    port_irq_restore(pm);
}

void robomas_set_angle_pid(robomas_motor_t *m, float kp, float ki, float kd) {
    if (m == NULL || !valid_gains(kp, ki, kd)) {
        lib_error_hook(LIB_ERR_PARAM, "robomas_set_angle_pid");
        return;
    }
    uint32_t pm = port_irq_save();
    m->angle_kp = kp;
    m->angle_ki = ki;
    m->angle_kd = kd;
    port_irq_restore(pm);
}

void robomas_set_angle_max_rpm(robomas_motor_t *m, float max_rpm) {
    if (m == NULL || !(max_rpm >= 0.0f)) { /* NaN も断る */
        lib_error_hook(LIB_ERR_PARAM, "robomas_set_angle_max_rpm");
        return;
    }
    uint32_t pm = port_irq_save();
    m->angle_max_rpm = max_rpm;
    m->angle_iterm = clampf(m->angle_iterm, max_rpm);
    port_irq_restore(pm);
}

void robomas_set_current_limit(robomas_motor_t *m, int16_t limit) {
    if (m == NULL || limit < 0 || limit > max_current(m->type)) {
        lib_error_hook(LIB_ERR_PARAM, "robomas_set_current_limit");
        return;
    }
    uint32_t pm = port_irq_save();
    m->current_limit = limit;
    port_irq_restore(pm);
}

void robomas_set_accel_limit(robomas_motor_t *m, float rpm_per_s) {
    if (m == NULL || !(rpm_per_s >= 0.0f)) {
        lib_error_hook(LIB_ERR_PARAM, "robomas_set_accel_limit");
        return;
    }
    uint32_t pm = port_irq_save();
    m->accel_limit = rpm_per_s;
    port_irq_restore(pm);
}

/*
 * 速度 PID。目標は出力軸 rpm。ギア比を掛けてローター rpm で計算するので、ギア比を変えてもゲインは同じ。
 * D は目標ではなく今のローター rpm の変化にかける (目標を急に変えても D が飛び出さない)
 */
static float speed_pid(robomas_motor_t *m, float out_rpm, float rotor_rpm) {
    float err = out_rpm * m->gear - rotor_rpm;
    m->integral = clampf(m->integral + err, m->speed_i_limit);
    float d = -(rotor_rpm - m->prev_rotor_rpm);
    return m->speed_kp * err + m->speed_ki * m->integral + m->speed_kd * d;
}

/*
 * 角度 PID。角度の差から目標の rpm (出力軸) を作り、最高 rpm で頭打ちにする。
 * I の分も最高 rpm で頭打ちにする (たまりすぎて行き過ぎないように)。D は今の角度の変化にかける
 */
static float angle_pid(robomas_motor_t *m, float angle) {
    float err = m->target - angle;
    m->angle_iterm = clampf(m->angle_iterm + m->angle_ki * err, m->angle_max_rpm);
    float d = -(angle - m->prev_angle);
    return clampf(m->angle_kp * err + m->angle_iterm + m->angle_kd * d, m->angle_max_rpm);
}

/*
 * 加速度の上限: 速度制御の目標を、1 回の計算 (1ms) ごとに (上限 / 1000) rpm までしか変えない。
 * 始め直すときは今の回転数から始める (回っている途中で切り替えても、0 rpm に向かって急ブレーキしない)
 */
static float ramp(robomas_motor_t *m, float target) {
    if (m->accel_limit > 0.0f) {
        m->ramp_rpm += clampf(target - m->ramp_rpm, m->accel_limit * 0.001f);
    } else {
        m->ramp_rpm = target;
    }
    return m->ramp_rpm;
}

/* 1 台分の電流指令を計算する。mode・target・integral を読み書きするので、割り込みを止めて呼ぶこと */
static int16_t compute(robomas_motor_t *m) {
    if (!robomas_ok(m)) {
        /* フィードバックが無いのに積分がたまり続けると、復帰した瞬間に暴れる。戻ってきたら、そのときの値から始め直す */
        reset_control(m);
        return 0;
    }
    float rotor_rpm = (float)m->fb_rpm;
    float angle = robomas_angle(m);
    if (m->restart) {
        m->ramp_rpm = rotor_rpm / m->gear; /* 回っている途中で切り替えても、0 rpm に向かって急ブレーキしない */
        m->prev_rotor_rpm = rotor_rpm;     /* 始めた瞬間に D が飛び出さない */
        m->prev_angle = angle;
        m->restart = false;
    }
    float cmd;
    switch (m->mode) {
    case ROBOMAS_MODE_SPEED:
        cmd = speed_pid(m, ramp(m, m->target), rotor_rpm);
        break;
    case ROBOMAS_MODE_ANGLE:
        /* 角度 PID で目標 rpm を作り、速度 PID に渡す */
        cmd = speed_pid(m, angle_pid(m, angle), rotor_rpm);
        break;
    case ROBOMAS_MODE_CURRENT:
    default:
        cmd = m->target;
        break;
    }
    m->prev_rotor_rpm = rotor_rpm;
    m->prev_angle = angle;
    return (int16_t)clampf(cmd, (float)m->current_limit);
}

static void put_be16(uint8_t *d, int offset, int16_t v) {
    d[offset] = (uint8_t)((uint16_t)v >> 8);
    d[offset + 1] = (uint8_t)v;
}

lib_status_t robomas_update(robomas_t *rm) {
    if (rm == NULL || rm->bus == NULL) {
        return port_fail(LIB_ERR_PARAM, "robomas_update");
    }
    uint8_t low[8] = {0};
    uint8_t high[8] = {0};
    bool use_low = false;
    bool use_high = false;
    uint32_t now = HAL_GetTick();
    /* 全モーターの計算を 1 回の割り込み禁止で行う (set_* と同時に走っても、種類と目標が食い違わない) */
    uint32_t pm = port_irq_save();
    /*
     * 計算して送るのは 1ms に 1 回だけ。待たずに何度も呼ばれたとき毎回送ると、ID の小さい指令 (0x200 / 0x1FF) が
     * CAN を埋めて ESC のフィードバック (0x201〜) が通れなくなり、robomas_ok が false になって全部止まる。
     * また積分は 1 回の計算ごとに足すので、呼ぶ回数でゲインの効き方が変わらないようにする
     */
    if (rm->updated && rm->last_update_ms == now) {
        port_irq_restore(pm);
        return LIB_OK;
    }
    rm->updated = true;
    rm->last_update_ms = now;
    for (int i = 0; i < ROBOMAS_MAX_MOTORS; i++) {
        robomas_motor_t *m = &rm->motors[i];
        if (!m->used) {
            continue;
        }
        m->output = compute(m);
        if (i < 4) {
            put_be16(low, i * 2, m->output);
            use_low = true;
        } else {
            put_be16(high, (i - 4) * 2, m->output);
            use_high = true;
        }
    }
    port_irq_restore(pm); /* can_send は自分で割り込みを止めるので、送る前に戻す */
    lib_status_t st = LIB_OK;
    if (use_low) {
        st = can_send(rm->bus, ROBOMAS_CMD_LOW, low, 8);
    }
    if (use_high) {
        lib_status_t st_high = can_send(rm->bus, ROBOMAS_CMD_HIGH, high, 8);
        if (st == LIB_OK) {
            st = st_high;
        }
    }
    return st;
}

void robomas_stop_all(robomas_t *rm) {
    if (rm == NULL) {
        return;
    }
    for (int i = 0; i < ROBOMAS_MAX_MOTORS; i++) {
        robomas_motor_t *m = &rm->motors[i];
        if (m->used) {
            uint32_t pm = port_irq_save(); /* 1 台ずつ止める (割り込みを止める時間を短く保つ) */
            m->mode = ROBOMAS_MODE_CURRENT;
            m->target = 0.0f;
            reset_control(m);
            port_irq_restore(pm);
        }
    }
}

#endif /* HAL_CAN_MODULE_ENABLED */
