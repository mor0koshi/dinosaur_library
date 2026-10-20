#ifndef FAKE_HAL_H
#define FAKE_HAL_H
/* HAL の偽物の状態。テストから値を入れたり、呼ばれた内容を確かめたりする */
#include "main.h"
#include "mor0koshi_common.h"

#define FAKE_MAX_SENT 64
#define FAKE_MAX_RXQ 16

typedef struct {
    CAN_TxHeaderTypeDef header;
    uint8_t data[8];
} fake_can_tx_t;

typedef struct {
    uint32_t tick;              /* HAL_GetTick の戻り値 */
    uint32_t primask;           /* 1 = 割り込み禁止中 */
    uint8_t nvic_enabled[128];  /* NVIC_GetEnableIRQ の戻り値 (fake_reset で全部 1) */

    /* CAN */
    int can_filter_calls;
    CAN_FilterTypeDef can_last_filter;
    int can_start_calls;
    uint32_t can_mcr_at_start;  /* HAL_CAN_Start が呼ばれた時点の MCR */
    uint32_t can_notifications;
    HAL_StatusTypeDef can_start_ret;
    uint32_t can_free_mailboxes; /* 空いているメールボックスの数 (fake_reset で 3。送るたびに減る) */
    fake_can_tx_t can_sent[FAKE_MAX_SENT];
    int can_sent_count;
    CAN_RxHeaderTypeDef can_rxq_header[FAKE_MAX_RXQ];
    uint8_t can_rxq_data[FAKE_MAX_RXQ][8];
    int can_rxq_count;

    /* UART */
    int uart_rx_start_calls;
    uint8_t *uart_rx_buf;
    uint16_t uart_rx_size;
    HAL_StatusTypeDef uart_rx_ret;

    /* TIM */
    int tim_start_calls;
    HAL_StatusTypeDef tim_start_ret;

    /* lib_error_hook */
    int error_count;
    lib_status_t last_error;
} fake_t;

extern fake_t fake;

void fake_reset(void);

/* 受信 FIFO にフレームを 1 つ積み、受信割り込み (HAL_CAN_RxFifo0MsgPendingCallback) を起こす */
void fake_can_receive(CAN_HandleTypeDef *hcan, uint32_t id, const uint8_t *data, uint8_t len);
/* メールボックスを 1 つ空けて、送信完了の割り込み (HAL_CAN_TxMailbox0CompleteCallback) を起こす */
void fake_can_tx_complete(CAN_HandleTypeDef *hcan);

#endif
