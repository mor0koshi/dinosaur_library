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
 * RX の DMA (Normal) と UART の割り込みをオンにする。F7 は「RX 反転」もオン。
 * F4 の UART には反転の機能がないので、基板にインバーターが要る。
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
#define SBUS_TIMEOUT_MS 200
#define SBUS_MAX_PORTS 2
#define SBUS_RXBUF_LEN 64

typedef struct sbus {
    uint8_t rxbuf[SBUS_RXBUF_LEN] __attribute__((aligned(32)));
    UART_HandleTypeDef *huart;
    volatile uint16_t isr_ch[SBUS_CHANNELS];
    volatile uint8_t isr_flags;
    volatile uint32_t isr_rx_ms;
    volatile bool isr_received;
    volatile uint32_t frame_count;
    volatile uint32_t error_count;
    uint16_t ch[SBUS_CHANNELS];
    bool ok;
    int stick_min;
    int stick_center;
    int stick_max;
    int deadband;
} sbus_t;

lib_status_t sbus_init(sbus_t *s, UART_HandleTypeDef *huart);
void sbus_update(sbus_t *s);
bool sbus_ok(const sbus_t *s);
int sbus_stick(const sbus_t *s, int ch);
int sbus_switch2(const sbus_t *s, int ch);
int sbus_switch3(const sbus_t *s, int ch);
int sbus_raw(const sbus_t *s, int ch);
void sbus_set_stick_range(sbus_t *s, int min, int center, int max);
void sbus_set_deadband(sbus_t *s, int deadband);
uint32_t sbus_frame_count(const sbus_t *s);
uint32_t sbus_error_count(const sbus_t *s);
void uart_rx_event_hook(UART_HandleTypeDef *huart, uint16_t size);
void uart_error_hook(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif

#endif
