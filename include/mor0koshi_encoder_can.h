#ifndef MOR0KOSHI_ENCODER_CAN_H
#define MOR0KOSHI_ENCODER_CAN_H
/*
 * エンコーダーの値を CAN で別の基板に送る・受け取る (使うときだけのおまけ)。
 * エンコーダー 1 個を 1 フレーム (8 バイト、リトルエンディアン) で送る。
 *   [0..3] int32 累積カウント / [4..7] float rpm
 *
 *   // 送る側: 10ms ごとに
 *   encoder_can_send(&can1, 0x300, &enc1);
 *   // 受ける側
 *   encoder_can_listen(&can1, 0x300, &roller1);
 *   float rpm = encoder_can_rpm(&roller1);
 *
 * ID はロボマス (0x1FF〜0x2FF) とぶつからない 0x300 番台がおすすめ。
 * 1Mbps で 1 フレーム約 0.13ms。6 個を 1ms ごとに送るとバスの約 8 割になるので、10ms ごと (約 8%) がおすすめ。
 */
#include "mor0koshi_can.h"
#include "mor0koshi_encoder.h"

#if defined(HAL_CAN_MODULE_ENABLED)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct encoder_can_rx {
    can_box_t box;
} encoder_can_rx_t;

#if defined(HAL_TIM_MODULE_ENABLED)
/* enc の累積カウントと rpm を id で送る */
lib_status_t encoder_can_send(can_t *bus, uint32_t id, const encoder_t *enc);
#endif

lib_status_t encoder_can_listen(can_t *bus, uint32_t id, encoder_can_rx_t *rx);
int32_t encoder_can_count(const encoder_can_rx_t *rx);     /* まだ届いていなければ 0 */
float encoder_can_rpm(const encoder_can_rx_t *rx);         /* まだ届いていなければ 0 */
uint32_t encoder_can_age_ms(const encoder_can_rx_t *rx);   /* 一度も届いていなければ UINT32_MAX */

#ifdef __cplusplus
}
#endif

#endif /* HAL_CAN_MODULE_ENABLED */

#endif
