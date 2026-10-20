#ifndef DINOSAUR_CAN_H
#define DINOSAUR_CAN_H
/*
 * CAN の送受信 (標準 ID のみ)。
 *
 *   can_t can1;
 *   can_init(&can1, &hcan1);              // フィルタ設定・開始・割り込み有効化まで全部やる
 *   can_send(&can1, 0x123, data, 8);      // 送る
 *   can_listen(&can1, 0x456, &box);       // 0x456 の最新の 1 フレームを box に取っておく
 *   if (can_read(&box, data)) { ... }     // 新しく届いていれば true
 *
 * CubeMX の準備: CAN を有効にして通信速度を決め、NVIC で RX0 と TX の割り込みをオンにする。
 * HAL_CAN_Start とフィルタの設定は自分で書かない (can_init がやる)。
 * can_init は 1 つの CAN につき 1 回、can_listen・can_subscribe・robomas_init より先に呼ぶ (後から呼ぶと登録が消える)。
 * 別の can_t で、登録済みの CAN を初期化すると LIB_ERR_PARAM。
 */
#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "dinosaur_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 詰める・取り出す (リトルエンディアン。offset はバイト位置) ---- */
void can_put_u8(uint8_t *d, int offset, uint8_t v);
void can_put_i8(uint8_t *d, int offset, int8_t v);
void can_put_u16(uint8_t *d, int offset, uint16_t v);
void can_put_i16(uint8_t *d, int offset, int16_t v);
void can_put_u32(uint8_t *d, int offset, uint32_t v);
void can_put_i32(uint8_t *d, int offset, int32_t v);
void can_put_float(uint8_t *d, int offset, float v);
uint8_t can_get_u8(const uint8_t *d, int offset);
int8_t can_get_i8(const uint8_t *d, int offset);
uint16_t can_get_u16(const uint8_t *d, int offset);
int16_t can_get_i16(const uint8_t *d, int offset);
uint32_t can_get_u32(const uint8_t *d, int offset);
int32_t can_get_i32(const uint8_t *d, int offset);
float can_get_float(const uint8_t *d, int offset);

#if defined(HAL_CAN_MODULE_ENABLED)

#define CAN_MAX_SUBS 16  /* 1 本の CAN に登録できる数 (listen・subscribe・subscribe_range の合計) */
#define CAN_TXQ_LEN 16   /* 送信待ちの列の長さ */
#define CAN_MAX_BUSES 3  /* 同時に使える CAN の本数 */

/* 受信したときに呼ばれる関数。受信割り込みの中で動くので短く書くこと。data は 8 バイト (len より後ろは 0) */
typedef void (*can_rx_fn)(uint32_t id, const uint8_t *data, uint8_t len, void *arg);

/* 最新の 1 フレームを取っておく箱 (can_listen で使う) */
typedef struct can_box {
    volatile uint8_t data[8];
    volatile uint8_t len;
    volatile bool fresh;     /* 前回の can_read のあとに届いたか */
    volatile bool received;  /* 一度でも届いたか */
    volatile uint32_t rx_ms; /* 最後に届いた時刻 (HAL_GetTick) */
} can_box_t;

/* ここから下の 2 つは can_t の中身。直接さわらない */
typedef struct {
    uint16_t first;
    uint16_t last;
    can_rx_fn fn;
    void *arg;
} can_sub_t;

typedef struct {
    uint16_t id;
    uint8_t len;
    uint8_t data[8];
} can_frame_t;

typedef struct can {
    CAN_HandleTypeDef *hcan;
    can_sub_t subs[CAN_MAX_SUBS];
    volatile uint8_t nsubs;
    can_frame_t txq[CAN_TXQ_LEN]; /* 送信待ちの列 (リングバッファ) */
    volatile uint8_t txq_head;
    volatile uint8_t txq_count;
    volatile uint32_t dropped;    /* 列があふれて捨てた数 */
    volatile bool any_rx;
    volatile uint32_t last_rx_ms;
} can_t;

lib_status_t can_init(can_t *bus, CAN_HandleTypeDef *hcan);

/* 送る。メールボックスが空いていなければ列に積み、送信完了の割り込みで順に出す。列があふれたら LIB_ERR_FULL */
lib_status_t can_send(can_t *bus, uint32_t id, const uint8_t *data, uint8_t len);

/* id の最新の 1 フレームを box に取っておく */
lib_status_t can_listen(can_t *bus, uint32_t id, can_box_t *box);
/* 前回の can_read のあとに新しく届いていれば、out に 8 バイト写して true */
bool can_read(can_box_t *box, uint8_t out[8]);
/* 新しいかどうかに関係なく最新の値を out に写す。一度も届いていなければ false */
bool can_peek(const can_box_t *box, uint8_t out[8]);
/* 最後に届いてからの ms。一度も届いていなければ UINT32_MAX */
uint32_t can_age_ms(const can_box_t *box);

/* id (または first〜last) が届いた瞬間に fn を呼ぶ (受信割り込みの中) */
lib_status_t can_subscribe(can_t *bus, uint32_t id, can_rx_fn fn, void *arg);
lib_status_t can_subscribe_range(can_t *bus, uint32_t first, uint32_t last, can_rx_fn fn, void *arg);

/* どの ID でも何か 1 つ届いてからの ms。一度も届いていなければ UINT32_MAX */
uint32_t can_bus_age_ms(const can_t *bus);
/* 送信待ちの列があふれて捨てた数 */
uint32_t can_dropped_count(const can_t *bus);

#endif /* HAL_CAN_MODULE_ENABLED */

#ifdef __cplusplus
}
#endif

#endif
