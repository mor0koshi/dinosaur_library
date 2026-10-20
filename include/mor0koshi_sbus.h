#ifndef MOR0KOSHI_SBUS_H
#define MOR0KOSHI_SBUS_H
/*
 * SBUS (プロポの受信機) の受信と、スティック・スイッチの値の変換。
 *
 *   sbus_t sbus;
 *   sbus_init(&sbus, &huart5);           // DMA 受信開始まで全部やる
 *   // メインループで毎周期 1 回
 *   sbus_update(&sbus);
 *   if (!sbus_ok(&sbus)) { 全部止める }
 *   int ly = sbus_stick(&sbus, 1);       // -1000〜+1000
 *
 * CubeMX の準備: UART を 100000bps・9bit (偶数パリティ込み)・ストップビット 2 にし、
 * RX の DMA (Normal。Circular は不可) と UART の割り込みをオンにする。F7 は「RX 反転」もオン。
 * F4 の UART には反転の機能がないので、基板にインバーターが要る。
 * sbus_init は 1 つの UART につき 1 回 (別の sbus_t で同じ UART を初期化すると LIB_ERR_PARAM)。
 *
 * sbus_init を呼ぶと、HAL_UARTEx_RxEventCallback と HAL_UART_ErrorCallback はライブラリが持つ。
 * 他の UART の分は uart_rx_event_hook / uart_error_hook に来るので、必要ならそちらを書く。
 */
#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "mor0koshi_common.h"

#if defined(HAL_UART_MODULE_ENABLED)

#ifdef __cplusplus
extern "C" {
#endif

#define SBUS_CHANNELS 16
#define SBUS_TIMEOUT_MS 200 /* これ以上フレームが来なければ NG (コネクタ抜け) */
#define SBUS_MAX_PORTS 2    /* 同時に使える SBUS の数 */
#define SBUS_RXBUF_LEN 64   /* DMA の受信バッファ。D キャッシュの都合で 32 の倍数 */

typedef struct sbus {
    uint8_t rxbuf[SBUS_RXBUF_LEN] __attribute__((aligned(32))); /* DMA が書く */
    UART_HandleTypeDef *huart;
    /* 受信割り込みが書く */
    volatile uint16_t isr_ch[SBUS_CHANNELS];
    volatile uint8_t isr_flags;
    volatile uint32_t isr_rx_ms;
    volatile bool isr_received;
    volatile uint32_t frame_count;
    volatile uint32_t error_count;
    /* sbus_update が書く (使う側はこちらを読む) */
    uint16_t ch[SBUS_CHANNELS];
    bool ok;
    /* 調整値 */
    int stick_min;
    int stick_center;
    int stick_max;
    int deadband;
} sbus_t;

lib_status_t sbus_init(sbus_t *s, UART_HandleTypeDef *huart);

/* 最新のフレームを写し取り、正常かどうかを判定する。メインループで毎周期 1 回呼ぶ */
void sbus_update(sbus_t *s);

/*
 * 次のどれかなら false: 1 フレームも来ていない / 200ms 以上来ていない / フェイルセーフ。
 * スイッチの押し直し (OFF にしたら解除) は、これが true のときだけ OFF とみなすこと。
 * NG の間はスイッチが 0 (OFF) と読めるので、それを OFF と扱うと ON のままでも解除されてしまう。
 */
bool sbus_ok(const sbus_t *s);

/* スティック: -1000〜+1000。中立 ± 不感帯は 0。sbus_ok が false なら 0 */
int sbus_stick(const sbus_t *s, int ch);
/* 2 段スイッチ: 中立より大きければ 1、それ以外は 0。sbus_ok が false なら 0 */
int sbus_switch2(const sbus_t *s, int ch);
/* 3 段スイッチ: 上の境目より大きければ +1、下の境目より小さければ -1、それ以外は 0。sbus_ok が false なら 0 */
int sbus_switch3(const sbus_t *s, int ch);
/* 生の値 (0〜2047)。sbus_ok が false でも最後の値を返す */
int sbus_raw(const sbus_t *s, int ch);

/* スティックの最小・中立・最大 (初期値 368 / 1024 / 1680)。スイッチの境目もここから決まる */
void sbus_set_stick_range(sbus_t *s, int min, int center, int max);
/* 中立からこの幅の内側は 0 (初期値 26) */
void sbus_set_deadband(sbus_t *s, int deadband);

uint32_t sbus_frame_count(const sbus_t *s); /* 受信したフレームの数 */
uint32_t sbus_error_count(const sbus_t *s); /* UART エラーの数 */

/* ライブラリが使わない UART の割り込みはここに来る。中身は空 (weak)。必要なら同じ名前で書く */
void uart_rx_event_hook(UART_HandleTypeDef *huart, uint16_t size);
void uart_error_hook(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_MODULE_ENABLED */

#endif
