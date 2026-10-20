/*
 * 例: 別の基板から CAN で送られてきたエンコーダーの値を使う (2026_B_767 のような基板)
 *
 * CubeMX の設定:
 *   - CAN1: 1Mbps、NVIC で CAN1 RX0 と CAN1 TX の割り込みをオン
 *
 * main.c の USER CODE BEGIN 2 で example_encoder_receiver_init() を 1 回、
 * while (1) の中で example_encoder_receiver_loop() を呼ぶ。
 */
#include "mor0koshi_library.h"

#if defined(HAL_CAN_MODULE_ENABLED)

extern CAN_HandleTypeDef hcan1;

static can_t can1;
static encoder_can_rx_t roller1;
static encoder_can_rx_t roller2;

/* デバッガのウォッチで見る用 */
volatile float example_roller1_rpm;
volatile float example_roller2_rpm;
volatile int example_encoder_link_ok;

void example_encoder_receiver_init(void) {
    can_init(&can1, &hcan1);
    encoder_can_listen(&can1, 0x300, &roller1);
    encoder_can_listen(&can1, 0x301, &roller2);
}

void example_encoder_receiver_loop(void) {
    /* 100ms 届かなければ断線とみなす */
    example_encoder_link_ok = encoder_can_age_ms(&roller1) < 100 && encoder_can_age_ms(&roller2) < 100;
    example_roller1_rpm = encoder_can_rpm(&roller1);
    example_roller2_rpm = encoder_can_rpm(&roller2);
}

#endif
