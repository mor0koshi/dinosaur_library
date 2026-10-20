/*
 * 例: プロポ (SBUS) でロボマスモーターを回す
 *
 * CubeMX の設定:
 *   - CAN1: 1Mbps、NVIC で CAN1 RX0 と CAN1 TX の割り込みをオン
 *   - UART5: 100000bps、9bit (偶数パリティ)、ストップビット 2、RX 反転 (F7)、RX の DMA (Normal)、UART5 の割り込みをオン
 *   - ESC: M3508 を ID 1、M2006 を ID 5 に設定しておく
 *
 * main.c の USER CODE BEGIN 2 で example_robomas_sbus_init() を 1 回、
 * while (1) の中で example_robomas_sbus_loop() を呼ぶ。
 */
#include "dinosaur_library.h"

#if defined(HAL_CAN_MODULE_ENABLED) && defined(HAL_UART_MODULE_ENABLED)

extern CAN_HandleTypeDef hcan1;
extern UART_HandleTypeDef huart5;

static can_t can1;
static sbus_t sbus;
static robomas_t rm;
static robomas_motor_t *wheel;
static robomas_motor_t *arm;
static uint32_t last_ms;

void example_robomas_sbus_init(void) {
    can_init(&can1, &hcan1);
    sbus_init(&sbus, &huart5);
    robomas_init(&rm, &can1);
    wheel = robomas_add(&rm, ROBOMAS_M3508, 1);
    arm = robomas_add(&rm, ROBOMAS_M2006, 5);
}

void example_robomas_sbus_loop(void) {
    uint32_t now = HAL_GetTick();
    if (now == last_ms) {
        return; /* 1ms に 1 回だけ */
    }
    last_ms = now;

    sbus_update(&sbus);
    if (!sbus_ok(&sbus)) {
        robomas_stop_all(&rm); /* プロポが切れたら全部止める */
    } else {
        /* 左スティック上下 (ch1) で M3508 を ±300rpm */
        robomas_set_rpm(wheel, (float)sbus_stick(&sbus, 1) * 0.3f);
        /* 3 段スイッチ (ch6) で M2006 を -90 / 0 / +90 度 */
        robomas_set_angle(arm, (float)sbus_switch3(&sbus, 6) * 90.0f);
    }
    robomas_update(&rm); /* 止めているときも呼び続ける (電流 0 を送り続ける) */
}

#endif
