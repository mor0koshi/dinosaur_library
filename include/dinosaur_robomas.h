#ifndef DINOSAUR_ROBOMAS_H
#define DINOSAUR_ROBOMAS_H
/*
 * ロボマスモーター M3508 (C620) / M2006 (C610) の速度制御・位置制御。
 *
 *   robomas_t rm;                                              // 1 本の CAN につき 1 個
 *   robomas_init(&rm, &can1);
 *   robomas_motor_t *wheel = robomas_add(&rm, ROBOMAS_M3508, 1);  // 種類と ESC の ID (1〜8)
 *   robomas_set_rpm(wheel, 300.0f);                            // 出力軸 rpm
 *   // 1ms ごとに 1 回 (メインループでも、タイマー割り込みの中でもよい)
 *   robomas_update(&rm);
 *
 * 使う側に見える値は全部「出力軸」(ギアの先)。ギア比は種類から自動で入る。
 * robomas_update をタイマー割り込みで呼んでも、メインループの robomas_set_* / robomas_stop_all と同時に動いて大丈夫
 * (指令の書き込みと update の計算は、割り込みを止めて行う)。
 * ゲインは 1ms ごとに robomas_update を呼ぶ前提。M3508 と位置制御の初期ゲインは仮の値なので実機で調整すること。
 * ロボマスは 1 台で 1Mbps の CAN の約 13% を使い、指令のフレームも 1ms ごとに送る (4 台で約 65%、5 台で約 91%)。
 * 1 本の CAN には 4 台くらいまでにする。
 * CAN の断線や ESC の電源断のあいだの回転は数えられないので、そのあとは robomas_reset_angle で原点を取り直す。
 */
#include <stdbool.h>
#include <stdint.h>

#include "dinosaur_can.h"

#if defined(HAL_CAN_MODULE_ENABLED)

#ifdef __cplusplus
extern "C" {
#endif

#define ROBOMAS_MAX_MOTORS 8
#define ROBOMAS_TIMEOUT_MS 100 /* フィードバックがこれ以上来なければ robomas_ok が false */

typedef enum {
    ROBOMAS_M3508 = 0, /* C620。ギア比 3591/187、電流 ±16384 */
    ROBOMAS_M2006 = 1, /* C610。ギア比 36、電流 ±10000 */
} robomas_type_t;

/* ここから下の 2 つは robomas_t の中身。直接さわらない */
typedef enum {
    ROBOMAS_MODE_CURRENT = 0,
    ROBOMAS_MODE_SPEED,
    ROBOMAS_MODE_ANGLE,
} robomas_mode_t;

typedef struct robomas_motor {
    bool used;
    robomas_type_t type;
    uint8_t id;    /* ESC の ID (1〜8) */
    float gear;    /* ギア比 (ローターの回転数 / 出力軸の回転数) */
    int16_t current_limit;
    float speed_kp;
    float speed_ki;
    float speed_i_limit;
    float angle_kp;
    float angle_max_rpm;
    robomas_mode_t mode;
    float target;   /* 電流: 生の値 / 速度: 出力軸 rpm / 位置: 出力軸の角度 [度] */
    float integral; /* 速度 PI の積分 (ローター rpm の誤差の和) */
    int16_t output; /* 最後に送った電流指令 */
    /* 受信割り込みが書く */
    volatile bool fb_started;
    volatile uint16_t fb_raw_angle;
    volatile int64_t fb_total; /* ローターの累積角度 (8192 で 1 回転) */
    volatile int16_t fb_rpm;   /* ローター rpm */
    volatile int16_t fb_current;
    volatile uint8_t fb_temp;
    volatile uint32_t fb_ms;
} robomas_motor_t;

typedef struct robomas {
    can_t *bus;
    robomas_motor_t motors[ROBOMAS_MAX_MOTORS]; /* motors[i] が ESC の ID i+1 */
} robomas_t;

lib_status_t robomas_init(robomas_t *rm, can_t *bus);
/* モーターを登録する。ID が範囲外 (1〜8 以外) か、登録済みなら NULL */
robomas_motor_t *robomas_add(robomas_t *rm, robomas_type_t type, int id);

/* 指令 (最後に呼んだものが有効。種類を変えると積分をリセットする) */
void robomas_set_current(robomas_motor_t *m, int16_t current); /* 電流を直接 (種類ごとの範囲で頭打ち) */
void robomas_set_rpm(robomas_motor_t *m, float rpm);           /* 速度制御: 出力軸 rpm */
void robomas_set_angle(robomas_motor_t *m, float deg);         /* 位置制御: 出力軸の角度 [度] */

/* 全モーターの制御を計算して、使っている ID の範囲の 0x200 / 0x1FF を送る。1ms ごとに呼ぶ */
lib_status_t robomas_update(robomas_t *rm);
/* 全モーターを電流指令 0 にして積分をリセットする (送るのは次の robomas_update)。SBUS が NG のときに呼ぶ */
void robomas_stop_all(robomas_t *rm);

float robomas_rpm(const robomas_motor_t *m);       /* 出力軸 rpm */
float robomas_angle(const robomas_motor_t *m);     /* 出力軸の累積角度 [度] (最初のフィードバックの位置が 0 度) */
void robomas_reset_angle(robomas_motor_t *m);      /* 今の位置を 0 度にする */
int16_t robomas_current(const robomas_motor_t *m); /* 実電流 (生の値) */
int robomas_temp(const robomas_motor_t *m);        /* 温度 ℃ (M3508 のみ。M2006 は 0) */
bool robomas_ok(const robomas_motor_t *m);         /* フィードバックが 100ms 以内に届いているか */

void robomas_set_gear_ratio(robomas_motor_t *m, float ratio);
void robomas_set_speed_gain(robomas_motor_t *m, float kp, float ki, float i_limit);
void robomas_set_angle_gain(robomas_motor_t *m, float kp, float max_rpm);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CAN_MODULE_ENABLED */

#endif
