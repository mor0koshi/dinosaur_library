/*
 * タイマーのエンコーダーモードで回転を数える。2026_B_446 の main.c (前回との差分) を元にしている。
 * rpm は update を呼んだ間隔を DWT のサイクルカウンタで測って計算するので、呼ぶ周期を指定しなくてよい。
 */
#include "mor0koshi_encoder.h"

#include <string.h>

#include "mor0koshi_port.h"

#if defined(HAL_TIM_MODULE_ENABLED)

lib_status_t encoder_init(encoder_t *e, TIM_HandleTypeDef *htim, int32_t counts_per_rev) {
    if (e == NULL || htim == NULL || counts_per_rev <= 0) {
        return port_fail(LIB_ERR_PARAM, "encoder_init");
    }
    memset(e, 0, sizeof *e);
    e->htim = htim;
    e->cpr = counts_per_rev;
    if (HAL_TIM_Encoder_Start(htim, TIM_CHANNEL_ALL) != HAL_OK) {
        return port_fail(LIB_ERR_HAL, "encoder_init");
    }
    port_cycles_enable();
    e->last_cnt = __HAL_TIM_GET_COUNTER(htim);
    e->last_cycles = port_cycles();
    e->last_ms = HAL_GetTick();
    return LIB_OK;
}

void encoder_set_reverse(encoder_t *e, bool reverse) {
    if (e != NULL) {
        e->reverse = reverse;
    }
}

void encoder_update(encoder_t *e) {
    if (e == NULL || e->htim == NULL) {
        return;
    }
    uint32_t cnt = __HAL_TIM_GET_COUNTER(e->htim);
    uint32_t cycles = port_cycles();
    uint32_t now_ms = HAL_GetTick();

    /*
     * タイマーは ARR の次で 0 に戻る。前回との差を ±半周に折り返して、0 をまたいでも正しく数える。
     * 32bit タイマーでも桁あふれしないよう 64bit で計算する。
     */
    int64_t period = (int64_t)__HAL_TIM_GET_AUTORELOAD(e->htim) + 1;
    int64_t diff = (int64_t)cnt - (int64_t)e->last_cnt;
    if (diff > period / 2) {
        diff -= period;
    } else if (diff < -period / 2) {
        diff += period;
    }
    if (e->reverse) {
        diff = -diff;
    }
    e->delta = (int32_t)diff;
    /* 符号なしで足してから戻す (int32 の端を越えても未定義動作にならず、回り込む) */
    e->count = (int32_t)((uint32_t)e->count + (uint32_t)e->delta);

    uint32_t dc = cycles - e->last_cycles; /* サイクルカウンタが一周しても、引き算なら正しい間隔になる */
    if (now_ms - e->last_ms >= ENCODER_RPM_RESET_MS) {
        e->rpm = 0.0f; /* 間隔が空きすぎて、サイクルカウンタが何周したか分からない */
    } else if (dc > 0U) {
        /* rpm = (delta / cpr) 回転 ÷ (dc / SystemCoreClock) 秒 × 60 */
        e->rpm = (float)e->delta * 60.0f * (float)SystemCoreClock / ((float)e->cpr * (float)dc);
    }
    e->last_cnt = cnt;
    e->last_cycles = cycles;
    e->last_ms = now_ms;
}

int32_t encoder_count(const encoder_t *e) { return e == NULL ? 0 : e->count; }
int32_t encoder_delta(const encoder_t *e) { return e == NULL ? 0 : e->delta; }
float encoder_rpm(const encoder_t *e) { return e == NULL ? 0.0f : e->rpm; }

float encoder_turns(const encoder_t *e) {
    if (e == NULL || e->cpr <= 0) {
        return 0.0f;
    }
    return (float)e->count / (float)e->cpr;
}

void encoder_reset(encoder_t *e) {
    if (e != NULL) {
        e->count = 0;
    }
}

#endif /* HAL_TIM_MODULE_ENABLED */
