#ifndef MOR0KOSHI_ENCODER_H
#define MOR0KOSHI_ENCODER_H
/*
 * タイマーのエンコーダーモードで回転を数える。CAN とは関係なく、その基板の中でそのまま使える。
 *
 *   encoder_t enc1;
 *   encoder_init(&enc1, &htim1, 2048);   // 2048 = 1 回転のカウント数 (4 逓倍込み)
 *   // 決まった周期 (1〜10ms くらい) で呼ぶ
 *   encoder_update(&enc1);
 *   float rpm = encoder_rpm(&enc1);
 *
 * CubeMX の準備: タイマーを Encoder Mode (TI1 and TI2)、Prescaler 0、Period を最大
 * (16bit なら 65535、TIM2 / TIM5 なら 0xFFFFFFFF) にする。割り込みは要らない。
 *
 * 遅い回転を 1ms 周期で測ると粗い (2048 カウントなら 1 カウント ≒ 29rpm)。その場合は 10ms 周期くらいにする。
 * encoder_update と値の読み出しは同じ場所 (メインループ同士、または割り込み同士) で行うこと。
 */
#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "mor0koshi_common.h"

#if defined(HAL_TIM_MODULE_ENABLED)

#ifdef __cplusplus
extern "C" {
#endif

#define ENCODER_RPM_RESET_MS 10000 /* update の間隔がこれ以上空いたら rpm を 0 にする (DWT が一周するため) */

typedef struct encoder {
    TIM_HandleTypeDef *htim;
    int32_t cpr; /* 1 回転のカウント数 (4 逓倍込み) */
    bool reverse;
    uint32_t last_cnt;    /* 前回のタイマーの値 */
    uint32_t last_cycles; /* 前回の DWT の値 */
    uint32_t last_ms;     /* 前回の HAL_GetTick */
    int32_t count;
    int32_t delta;
    float rpm;
} encoder_t;

lib_status_t encoder_init(encoder_t *e, TIM_HandleTypeDef *htim, int32_t counts_per_rev);
/* 向きを逆にする (これから先の変化量の符号が変わる) */
void encoder_set_reverse(encoder_t *e, bool reverse);
/* タイマーを読んで、累積カウント・変化量・rpm を更新する */
void encoder_update(encoder_t *e);

int32_t encoder_count(const encoder_t *e); /* 起動 (または reset) からの累積カウント */
int32_t encoder_delta(const encoder_t *e); /* 前回の update からの変化量 */
float encoder_rpm(const encoder_t *e);     /* 回転数 (符号付き) */
float encoder_turns(const encoder_t *e);   /* 累積の回転数 (count / 1 回転のカウント数) */
void encoder_reset(encoder_t *e);          /* 累積を 0 に戻す (原点合わせ) */

#ifdef __cplusplus
}
#endif

#endif /* HAL_TIM_MODULE_ENABLED */

#endif
