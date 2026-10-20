/*
 * 例: エンコーダー 2 個の値を CAN で別の基板に送る (2026_B_446 のような基板)
 *
 * CubeMX の設定:
 *   - CAN1: 1Mbps、NVIC で CAN1 RX0 と CAN1 TX の割り込みをオン
 *   - TIM1・TIM2: Encoder Mode (TI1 and TI2)、Prescaler 0、Period 最大
 *
 * main.c の USER CODE BEGIN 2 で example_encoder_sender_init() を 1 回、
 * while (1) の中で example_encoder_sender_loop() を呼ぶ。
 */
#include "mor0koshi_library.h"

#if defined(HAL_CAN_MODULE_ENABLED) && defined(HAL_TIM_MODULE_ENABLED)

extern CAN_HandleTypeDef hcan1;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;

static can_t can1;
static encoder_t enc1;
static encoder_t enc2;
static uint32_t last_ms;

void example_encoder_sender_init(void) {
    can_init(&can1, &hcan1);
    encoder_init(&enc1, &htim1, 2048); /* 512 パルスのエンコーダー × 4 逓倍 */
    encoder_init(&enc2, &htim2, 2048);
}

void example_encoder_sender_loop(void) {
    uint32_t now = HAL_GetTick();
    if (now - last_ms < 10) {
        return; /* 10ms ごと (2 個で CAN の約 3%) */
    }
    last_ms = now;
    encoder_update(&enc1);
    encoder_update(&enc2);
    encoder_can_send(&can1, 0x300, &enc1);
    encoder_can_send(&can1, 0x301, &enc2);
}

#endif
