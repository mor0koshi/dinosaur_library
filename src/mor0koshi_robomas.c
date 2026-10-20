/*
 * ロボマスモーター M3508 (C620) / M2006 (C610)。AIcontest の main.c を元にしている。
 *   指令: 0x200 (ESC の ID 1〜4)、0x1FF (ID 5〜8)。各 2 バイトの int16、ビッグエンディアン
 *   フィードバック: 0x200 + ID。[0..1] 角度 0〜8191、[2..3] ローター rpm、[4..5] 電流、[6] 温度 (C620 のみ)
 */
#include "mor0koshi_robomas.h"

#include <string.h>

#include "mor0koshi_port.h"

#if defined(HAL_CAN_MODULE_ENABLED)

#define ROBOMAS_FB_FIRST 0x201U
#define ROBOMAS_FB_LAST 0x208U
#define ROBOMAS_CMD_LOW 0x200U  /* ESC の ID 1〜4 への電流指令 */
#define ROBOMAS_CMD_HIGH 0x1FFU /* ESC の ID 5〜8 への電流指令 */
#define ROBOMAS_COUNTS_PER_REV 8192

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
    if (rm == NULL || bus == NULL) {
        return port_fail(LIB_ERR_PARAM, "robomas_init");
    }
    memset(rm, 0, sizeof *rm);
    rm->bus = bus;
    return can_subscribe_range(bus, ROBOMAS_FB_FIRST, ROBOMAS_FB_LAST, on_feedback, rm);
}

robomas_motor_t *robomas_add(robomas_t *rm, robomas_type_t type, int id) {
    if (rm == NULL || id < 1 || id > ROBOMAS_MAX_MOTORS || (type != ROBOMAS_M3508 && type != ROBOMAS_M2006) ||
        rm->motors[id - 1].used) {
        lib_error_hook(LIB_ERR_PARAM, "robomas_add");
        return NULL;
    }
    robomas_motor_t *m = &rm->motors[id - 1];
    uint32_t pm = port_irq_save();
    memset(m, 0, sizeof *m);
    m->type = type;
    m->id = (uint8_t)id;
    if (type == ROBOMAS_M3508) {
        m->gear = 3591.0f / 187.0f;
        m->current_limit = 16384;
    } else {
        m->gear = 36.0f;
        m->current_limit = 10000;
    }
    /* M2006 の速度ゲインは AIcontest で確認済みの値。M3508 と位置制御は仮の値 (実機で調整する) */
    m->speed_kp = 1.5f;
    m->speed_ki = 0.08f;
    m->speed_i_limit = 5000.0f;
    m->angle_kp = 2.0f;
    m->angle_max_rpm = 100.0f;
    m->mode = ROBOMAS_MODE_CURRENT;
    m->target = 0.0f;
    m->used = true; /* 設定を書き終えてから使う印を立てる (割り込みが途中の状態を見ないように) */
    port_irq_restore(pm);
    return m;
}

/* 制御の種類が変わったら積分を捨てる (前の種類の積分が残ると、切り替えた瞬間に飛び出す) */
static void set_mode(robomas_motor_t *m, robomas_mode_t mode) {
    if (m->mode != mode) {
        m->mode = mode;
        m->integral = 0.0f;
    }
}

void robomas_set_current(robomas_motor_t *m, int16_t current) {
    if (m == NULL) {
        return;
    }
    set_mode(m, ROBOMAS_MODE_CURRENT);
    m->target = (float)current;
}

void robomas_set_rpm(robomas_motor_t *m, float rpm) {
    if (m == NULL) {
        return;
    }
    set_mode(m, ROBOMAS_MODE_SPEED);
    m->target = rpm;
}

void robomas_set_angle(robomas_motor_t *m, float deg) {
    if (m == NULL) {
        return;
    }
    set_mode(m, ROBOMAS_MODE_ANGLE);
    m->target = deg;
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
    port_irq_restore(pm);
}

int16_t robomas_current(const robomas_motor_t *m) { return m == NULL ? 0 : m->fb_current; }

int robomas_temp(const robomas_motor_t *m) {
    if (m == NULL || m->type == ROBOMAS_M2006) {
        return 0; /* C610 は温度を返さない */
    }
    return m->fb_temp;
}

void robomas_set_gear_ratio(robomas_motor_t *m, float ratio) {
    if (m == NULL || !(ratio > 0.0f)) {
        lib_error_hook(LIB_ERR_PARAM, "robomas_set_gear_ratio");
        return;
    }
    m->gear = ratio;
}

void robomas_set_speed_gain(robomas_motor_t *m, float kp, float ki, float i_limit) {
    if (m == NULL || i_limit < 0.0f) {
        lib_error_hook(LIB_ERR_PARAM, "robomas_set_speed_gain");
        return;
    }
    m->speed_kp = kp;
    m->speed_ki = ki;
    m->speed_i_limit = i_limit;
    m->integral = clampf(m->integral, i_limit);
}

void robomas_set_angle_gain(robomas_motor_t *m, float kp, float max_rpm) {
    if (m == NULL || max_rpm < 0.0f) {
        lib_error_hook(LIB_ERR_PARAM, "robomas_set_angle_gain");
        return;
    }
    m->angle_kp = kp;
    m->angle_max_rpm = max_rpm;
}

/* 速度 PI。目標は出力軸 rpm。ギア比を掛けてローター rpm で計算するので、ギア比を変えてもゲインは同じ */
static float speed_pi(robomas_motor_t *m, float out_rpm) {
    float err = out_rpm * m->gear - (float)m->fb_rpm;
    m->integral = clampf(m->integral + err, m->speed_i_limit);
    return m->speed_kp * err + m->speed_ki * m->integral;
}

static int16_t compute(robomas_motor_t *m) {
    if (!robomas_ok(m)) {
        /* フィードバックが無いのに積分がたまり続けると、復帰した瞬間に暴れる */
        m->integral = 0.0f;
        return 0;
    }
    float cmd;
    switch (m->mode) {
    case ROBOMAS_MODE_SPEED:
        cmd = speed_pi(m, m->target);
        break;
    case ROBOMAS_MODE_ANGLE:
        /* 角度の差に比例した目標 rpm を作り (上限で頭打ち)、速度制御に渡す */
        cmd = speed_pi(m, clampf(m->angle_kp * (m->target - robomas_angle(m)), m->angle_max_rpm));
        break;
    case ROBOMAS_MODE_CURRENT:
    default:
        cmd = m->target;
        break;
    }
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
            m->mode = ROBOMAS_MODE_CURRENT;
            m->target = 0.0f;
            m->integral = 0.0f;
        }
    }
}

#endif /* HAL_CAN_MODULE_ENABLED */
