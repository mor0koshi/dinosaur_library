/* arm_build_check.sh の link で足すファイル。ライブラリの全部品をリンクさせる (実行はしない) */
#include "dinosaur_library.h"

extern CAN_HandleTypeDef hcan1;
extern TIM_HandleTypeDef htim1;
extern UART_HandleTypeDef huart2;

static can_t can1;
static encoder_t enc;
static sbus_t sbus;
static robomas_t rm;
static encoder_can_rx_t rx;

void linkcheck(void) {
    can_init(&can1, &hcan1);
    encoder_init(&enc, &htim1, 2048);
    encoder_update(&enc);
    encoder_can_send(&can1, 0x300, &enc);
    encoder_can_listen(&can1, 0x301, &rx);
    sbus_init(&sbus, &huart2);
    sbus_update(&sbus);
    robomas_init(&rm, &can1);
    robomas_set_rpm(robomas_add(&rm, ROBOMAS_M3508, 1), 10.0f);
    robomas_update(&rm);
}
