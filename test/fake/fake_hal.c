/* HAL の偽物の中身。呼ばれた内容を fake に記録し、fake に入れた値を返す */
#include "fake_hal.h"

#include <string.h>

fake_t fake;
DWT_Type fake_dwt;
CoreDebug_Type fake_coredebug;
uint32_t SystemCoreClock;
CAN_TypeDef fake_can1, fake_can2, fake_can3;

void fake_reset(void) {
    memset(&fake, 0, sizeof fake);
    memset(fake.nvic_enabled, 1, sizeof fake.nvic_enabled);
    fake.can_free_mailboxes = 3;
    fake.last_error = LIB_OK;
    memset(&fake_dwt, 0, sizeof fake_dwt);
    memset(&fake_coredebug, 0, sizeof fake_coredebug);
    memset(&fake_can1, 0, sizeof fake_can1);
    memset(&fake_can2, 0, sizeof fake_can2);
    memset(&fake_can3, 0, sizeof fake_can3);
    SystemCoreClock = 100000000U; /* 100MHz (計算しやすい値) */
}

/* テストではライブラリの weak な lib_error_hook をこれで上書きして、呼ばれたことを数える */
void lib_error_hook(lib_status_t err, const char *where) {
    fake.last_where = where;
    fake.error_count++;
    fake.last_error = err;
}

uint32_t HAL_GetTick(void) { return fake.tick; }

uint32_t NVIC_GetEnableIRQ(IRQn_Type IRQn) { return fake.nvic_enabled[IRQn]; }
uint32_t __get_PRIMASK(void) { return fake.primask; }
void __set_PRIMASK(uint32_t priMask) { fake.primask = priMask; }
void __disable_irq(void) {
    fake.primask = 1;
    fake.irq_disable_calls++;
}
void __enable_irq(void) { fake.primask = 0; }

HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *hcan, const CAN_FilterTypeDef *sFilterConfig) {
    (void)hcan;
    fake.can_filter_calls++;
    fake.can_last_filter = *sFilterConfig;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *hcan) {
    fake.can_start_calls++;
    fake.can_mcr_at_start = hcan->Instance->MCR;
    return fake.can_start_ret;
}

HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *hcan, uint32_t ActiveITs) {
    (void)hcan;
    fake.can_notifications |= ActiveITs;
    return HAL_OK;
}

uint32_t HAL_CAN_GetTxMailboxesFreeLevel(const CAN_HandleTypeDef *hcan) {
    (void)hcan;
    return fake.can_free_mailboxes;
}

HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *hcan, const CAN_TxHeaderTypeDef *pHeader,
                                       const uint8_t aData[], uint32_t *pTxMailbox) {
    (void)hcan;
    if (fake.can_free_mailboxes == 0 || fake.can_sent_count >= FAKE_MAX_SENT) {
        return HAL_ERROR;
    }
    fake.can_free_mailboxes--;
    fake_can_tx_t *t = &fake.can_sent[fake.can_sent_count++];
    t->header = *pHeader;
    memset(t->data, 0, sizeof t->data);
    memcpy(t->data, aData, pHeader->DLC);
    *pTxMailbox = 1;
    return HAL_OK;
}

uint32_t HAL_CAN_GetRxFifoFillLevel(const CAN_HandleTypeDef *hcan, uint32_t RxFifo) {
    (void)hcan;
    (void)RxFifo;
    return (uint32_t)fake.can_rxq_count;
}

HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *hcan, uint32_t RxFifo, CAN_RxHeaderTypeDef *pHeader,
                                       uint8_t aData[]) {
    (void)hcan;
    (void)RxFifo;
    if (fake.can_rxq_count == 0) {
        return HAL_ERROR;
    }
    *pHeader = fake.can_rxq_header[0];
    memcpy(aData, fake.can_rxq_data[0], 8);
    fake.can_rxq_count--;
    memmove(&fake.can_rxq_header[0], &fake.can_rxq_header[1], sizeof fake.can_rxq_header[0] * (size_t)fake.can_rxq_count);
    memmove(&fake.can_rxq_data[0], &fake.can_rxq_data[1], sizeof fake.can_rxq_data[0] * (size_t)fake.can_rxq_count);
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size) {
    (void)huart;
    fake.uart_rx_start_calls++;
    fake.uart_rx_buf = pData;
    fake.uart_rx_size = Size;
    return fake.uart_rx_ret;
}

HAL_StatusTypeDef HAL_TIM_Encoder_Start(TIM_HandleTypeDef *htim, uint32_t Channel) {
    (void)htim;
    (void)Channel;
    fake.tim_start_calls++;
    return fake.tim_start_ret;
}
