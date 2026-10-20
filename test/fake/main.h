#ifndef FAKE_MAIN_H
#define FAKE_MAIN_H
/*
 * テスト用の HAL の偽物。ライブラリが使う型・定数・関数だけを、本物と同じ名前で用意する。
 * 本物の CubeMX プロジェクトでは、この代わりに Core/Inc/main.h (→ stm32xxxx_hal.h) が入る。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HAL_CAN_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED

typedef enum { HAL_OK = 0x00U, HAL_ERROR = 0x01U, HAL_BUSY = 0x02U, HAL_TIMEOUT = 0x03U } HAL_StatusTypeDef;
typedef enum { DISABLE = 0U, ENABLE = 1U } FunctionalState;

uint32_t HAL_GetTick(void);

/* ---- CMSIS ---- */
#define __CORTEX_M 4U

typedef enum {
    CAN1_TX_IRQn = 19,
    CAN1_RX0_IRQn = 20,
    CAN2_TX_IRQn = 63,
    CAN2_RX0_IRQn = 64,
    CAN3_TX_IRQn = 104,
    CAN3_RX0_IRQn = 105,
} IRQn_Type;

uint32_t NVIC_GetEnableIRQ(IRQn_Type IRQn);
uint32_t __get_PRIMASK(void);
void __set_PRIMASK(uint32_t priMask);
void __disable_irq(void);
void __enable_irq(void);

typedef struct {
    uint32_t CTRL;
    uint32_t CYCCNT;
} DWT_Type;
typedef struct {
    uint32_t DEMCR;
} CoreDebug_Type;
extern DWT_Type fake_dwt;
extern CoreDebug_Type fake_coredebug;
#define DWT (&fake_dwt)
#define CoreDebug (&fake_coredebug)
#define DWT_CTRL_CYCCNTENA_Msk (1UL)
#define CoreDebug_DEMCR_TRCENA_Msk (1UL << 24)
extern uint32_t SystemCoreClock;

/* ---- CAN ---- */
typedef struct {
    uint32_t MCR;
} CAN_TypeDef;
extern CAN_TypeDef fake_can1, fake_can2, fake_can3;
#define CAN1 (&fake_can1)
#define CAN2 (&fake_can2)
#define CAN3 (&fake_can3)
#define CAN_MCR_ABOM (1UL << 6)

typedef struct {
    CAN_TypeDef *Instance;
} CAN_HandleTypeDef;

typedef struct {
    uint32_t FilterIdHigh;
    uint32_t FilterIdLow;
    uint32_t FilterMaskIdHigh;
    uint32_t FilterMaskIdLow;
    uint32_t FilterFIFOAssignment;
    uint32_t FilterBank;
    uint32_t FilterMode;
    uint32_t FilterScale;
    uint32_t FilterActivation;
    uint32_t SlaveStartFilterBank;
} CAN_FilterTypeDef;

typedef struct {
    uint32_t StdId;
    uint32_t ExtId;
    uint32_t IDE;
    uint32_t RTR;
    uint32_t DLC;
    FunctionalState TransmitGlobalTime;
} CAN_TxHeaderTypeDef;

typedef struct {
    uint32_t StdId;
    uint32_t ExtId;
    uint32_t IDE;
    uint32_t RTR;
    uint32_t DLC;
    uint32_t Timestamp;
    uint32_t FilterMatchIndex;
} CAN_RxHeaderTypeDef;

#define CAN_ID_STD 0x00000000U
#define CAN_ID_EXT 0x00000004U
#define CAN_RTR_DATA 0x00000000U
#define CAN_RTR_REMOTE 0x00000002U
#define CAN_RX_FIFO0 0x00000000U
#define CAN_FILTER_FIFO0 0x00000000U
#define CAN_FILTERMODE_IDMASK 0x00000000U
#define CAN_FILTERSCALE_32BIT 0x00000001U
#define CAN_IT_TX_MAILBOX_EMPTY 0x00000001U
#define CAN_IT_RX_FIFO0_MSG_PENDING 0x00000002U

HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *hcan, const CAN_FilterTypeDef *sFilterConfig);
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *hcan, uint32_t ActiveITs);
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(const CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *hcan, const CAN_TxHeaderTypeDef *pHeader,
                                       const uint8_t aData[], uint32_t *pTxMailbox);
uint32_t HAL_CAN_GetRxFifoFillLevel(const CAN_HandleTypeDef *hcan, uint32_t RxFifo);
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *hcan, uint32_t RxFifo, CAN_RxHeaderTypeDef *pHeader,
                                       uint8_t aData[]);
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan);
void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef *hcan);
void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef *hcan);
void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef *hcan);
void HAL_CAN_TxMailbox0AbortCallback(CAN_HandleTypeDef *hcan);
void HAL_CAN_TxMailbox1AbortCallback(CAN_HandleTypeDef *hcan);
void HAL_CAN_TxMailbox2AbortCallback(CAN_HandleTypeDef *hcan);

/* ---- DMA / UART ---- */
typedef struct {
    int ht_disabled; /* __HAL_DMA_DISABLE_IT(h, DMA_IT_HT) が呼ばれたら 1 */
} DMA_HandleTypeDef;
#define DMA_IT_HT 0x00000008U
#define __HAL_DMA_DISABLE_IT(__HANDLE__, __INTERRUPT__) ((void)(__INTERRUPT__), (__HANDLE__)->ht_disabled = 1)

typedef struct {
    int dummy;
} USART_TypeDef;
typedef struct {
    USART_TypeDef *Instance;
    DMA_HandleTypeDef *hdmarx;
} UART_HandleTypeDef;

HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size);
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

/* ---- TIM ---- */
typedef struct {
    uint32_t CNT;
    uint32_t ARR;
} TIM_TypeDef;
typedef struct {
    TIM_TypeDef *Instance;
} TIM_HandleTypeDef;
#define TIM_CHANNEL_ALL 0x0000003CU
#define __HAL_TIM_GET_COUNTER(__HANDLE__) ((__HANDLE__)->Instance->CNT)
#define __HAL_TIM_GET_AUTORELOAD(__HANDLE__) ((__HANDLE__)->Instance->ARR)
HAL_StatusTypeDef HAL_TIM_Encoder_Start(TIM_HandleTypeDef *htim, uint32_t Channel);

#endif
