/* arm_build_check.sh の link で足すファイル。CAN が無効のプロジェクトで、SBUS とエンコーダーだけ使う */
#include "dinosaur_library.h"

extern TIM_HandleTypeDef htim3;
extern UART_HandleTypeDef huart3;

static encoder_t enc;
static sbus_t sbus;

void linkcheck(void) {
    encoder_init(&enc, &htim3, 2048);
    encoder_update(&enc);
    sbus_init(&sbus, &huart3);
    sbus_update(&sbus);
}
