# mor0koshi_library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** STM32 HAL のプロジェクトで、CAN・SBUS・エンコーダー・ロボマスモーター (M3508 / M2006) を `init` を呼ぶだけで使えるようにする C のライブラリを作る。

**Architecture:** 独立した git リポジトリの静的ライブラリ。各 .c は `HAL_*_MODULE_ENABLED` で使わない部品を外す。HAL の割り込みコールバックは、それを使う部品の .c に置いてライブラリが振り分ける (呼んだ部品の .c だけがリンクされる)。テストは HAL の偽物 (`test/fake/`) を使って PC の gcc で走らせ、最後に本物の CubeMX プロジェクトの設定で ARM のビルドを確かめる。

**Tech Stack:** C11、STM32 HAL (F4 / F7、bxCAN)、CMake 3.22 + Ninja、arm-none-eabi-gcc、PC のテストは gcc + ctest + AddressSanitizer / UBSan。

**Spec:** `docs/specs/2026-10-20-mor0koshi-library-design.md` (実装する人は必ずこちらも読む)

## Global Constraints

- リポジトリの場所: `/home/fukui-ros/STM32 project/mor0koshi_library` (**パスに空白がある**のでシェルでは必ずクォートする)。ブランチは `main`
- 言語は C11。malloc は使わない。ライブラリの中で `Error_Handler()` は呼ばない
- **関数と型の頭には何も付けない** (`can_send`、`sbus_stick`、`encoder_rpm`、`robomas_set_rpm`)。**ファイル名には `mor0koshi_` を付ける**
- 各 .c と公開ヘッダーの中身は、必要な HAL モジュールのマクロ (`HAL_CAN_MODULE_ENABLED` / `HAL_UART_MODULE_ENABLED` / `HAL_TIM_MODULE_ENABLED`) で囲む
- HAL には `#include "main.h"` を通して触る (テストでは `test/fake/main.h` が入る)
- 失敗したときは `lib_status_t` を返し (`robomas_add` は NULL)、**必ず** `lib_error_hook(err, "関数名")` も呼ぶ
- NULL を渡された関数は何もしない (値を返す関数は 0。`*_age_ms` だけは UINT32_MAX)
- 割り込みの中ではデータを写すだけ。例外はロボマスの角度の積算 (受信割り込みの中で行う)
- 割り込みと共有するデータは `port_irq_save()` / `port_irq_restore()` で囲んで読み書きする
- float には必ず `f` を付ける (F4 の FPU は単精度。ARM では `-Wdouble-promotion` で確かめる)
- コメントは日本語で、2026_B_767 と同じく「なぜそうするか」を短く書く
- PC のテストは `-Wall -Wextra -Werror` + ASan / UBSan で通す。ARM ではライブラリと例のファイルに警告が 1 つも出ないこと (`-Wall -Wextra -Wdouble-promotion`)
- コミットメッセージは日本語。最後の行に `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` を付ける

**テストの走らせ方 (全タスク共通):**

```bash
cd "/home/fukui-ros/STM32 project/mor0koshi_library"
cmake -S test -B build-test -G Ninja && cmake --build build-test && ctest --test-dir build-test --output-on-failure
```

---

## ファイル構成

| ファイル | 役割 | タスク |
|---|---|---|
| `.gitignore` | `build/`・`build-test/` を無視 | 1 |
| `CMakeLists.txt` | 静的ライブラリ `mor0koshi_library` と、例のビルド確認用 `mor0koshi_examples` | 1 (以後ソースを足す)、7 |
| `include/mor0koshi_common.h` | `lib_status_t`、`lib_error_hook` | 1 |
| `include/mor0koshi_library.h` | まとめヘッダー | 1 (以後 include を足す) |
| `src/mor0koshi_common.c` | `lib_error_hook` の空の weak 定義 | 1 |
| `src/mor0koshi_port.h` | 割り込みの禁止・復帰、`port_fail`、DWT | 1、4 |
| `include/mor0koshi_can.h` / `src/mor0koshi_can.c` | CAN | 2 |
| `include/mor0koshi_sbus.h` / `src/mor0koshi_sbus.c` | SBUS | 3 |
| `include/mor0koshi_encoder.h` / `src/mor0koshi_encoder.c` | エンコーダー | 4 |
| `include/mor0koshi_encoder_can.h` / `src/mor0koshi_encoder_can.c` | エンコーダーの値を CAN で送る・受ける | 5 |
| `include/mor0koshi_robomas.h` / `src/mor0koshi_robomas.c` | ロボマス | 6 |
| `examples/*.c` | 例 3 つ | 7 |
| `test/CMakeLists.txt`、`test/test.h` | PC のテストの枠組み | 1 |
| `test/fake/main.h`、`test/fake/fake_hal.h`、`test/fake/fake_hal.c` | HAL の偽物 | 1 |
| `test/fake/fake_can.c` | 偽物から CAN の割り込みを起こす補助 | 2 |
| `test/test_*.c` | 部品ごとのテスト | 1〜6 |
| `test/arm_build_check.sh`、`test/linkcheck_*.c` | ARM のビルド確認 | 7 |
| `README.md` | 使い方・実機確認の手順 | 8 |

---

### Task 1: 土台 (共通の型、割り込みの補助、HAL の偽物、テストの枠組み)

**Files:**
- Create: `.gitignore`、`CMakeLists.txt`、`include/mor0koshi_common.h`、`include/mor0koshi_library.h`、`src/mor0koshi_common.c`、`src/mor0koshi_port.h`
- Create: `test/CMakeLists.txt`、`test/test.h`、`test/fake/main.h`、`test/fake/fake_hal.h`、`test/fake/fake_hal.c`
- Test: `test/test_port.c`

**Interfaces:**
- Consumes: なし
- Produces:
  - `typedef enum { LIB_OK = 0, LIB_ERR_PARAM, LIB_ERR_FULL, LIB_ERR_HAL, LIB_ERR_IRQ_OFF } lib_status_t;`
  - `void lib_error_hook(lib_status_t err, const char *where);` (weak)
  - `static inline uint32_t port_irq_save(void);` / `static inline void port_irq_restore(uint32_t primask);`
  - `static inline lib_status_t port_fail(lib_status_t err, const char *where);` (hook を呼んで err を返す)
  - テスト: `fake_t fake;`、`void fake_reset(void);`、`CHECK` / `CHECK_EQ` / `CHECK_NEAR` / `RUN` / `TEST_RESULT`、CMake 関数 `add_lib_test(name)` と変数 `LIB_SOURCES`

- [ ] **Step 1: テストの枠組みと HAL の偽物を作る**

`.gitignore`:

```
build/
build-test/
```

`test/test.h`:

```c
#ifndef TEST_H
#define TEST_H
/* 小さなテストの枠組み。失敗した CHECK を表示して数え、最後に main の戻り値にする */
#include <math.h>
#include <stdio.h>

#include "fake_hal.h"

static int test_failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            test_failures++;                                                \
        }                                                                   \
    } while (0)

#define CHECK_EQ(a, b)                                                      \
    do {                                                                    \
        long long a_ = (long long)(a), b_ = (long long)(b);                 \
        if (a_ != b_) {                                                     \
            printf("%s:%d: %s == %s failed (%lld != %lld)\n", __FILE__,     \
                   __LINE__, #a, #b, a_, b_);                               \
            test_failures++;                                                \
        }                                                                   \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                               \
    do {                                                                    \
        double a_ = (double)(a), b_ = (double)(b);                          \
        if (fabs(a_ - b_) > (eps)) {                                        \
            printf("%s:%d: %s ~= %s failed (%.9g != %.9g)\n", __FILE__,     \
                   __LINE__, #a, #b, a_, b_);                               \
            test_failures++;                                                \
        }                                                                   \
    } while (0)

/* テスト 1 つごとに偽物の状態を初期化してから走らせる */
#define RUN(test)     \
    do {              \
        fake_reset(); \
        test();       \
    } while (0)

#define TEST_RESULT() (test_failures == 0 ? 0 : 1)

#endif
```

`test/fake/main.h`:

```c
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
```

`test/fake/fake_hal.h`:

```c
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
```

`test/fake/fake_hal.c`:

```c
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
    (void)where;
    fake.error_count++;
    fake.last_error = err;
}

uint32_t HAL_GetTick(void) { return fake.tick; }

uint32_t NVIC_GetEnableIRQ(IRQn_Type IRQn) { return fake.nvic_enabled[IRQn]; }
uint32_t __get_PRIMASK(void) { return fake.primask; }
void __set_PRIMASK(uint32_t priMask) { fake.primask = priMask; }
void __disable_irq(void) { fake.primask = 1; }
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
```

`test/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.22)
project(mor0koshi_library_test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()

set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ライブラリのソースと HAL の偽物。テストの実行ファイルごとに全部まとめてリンクする
set(LIB_SOURCES
    ${LIB_DIR}/src/mor0koshi_common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fake/fake_hal.c
)

function(add_lib_test name)
    add_executable(${name} ${CMAKE_CURRENT_SOURCE_DIR}/${name}.c ${LIB_SOURCES})
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/fake
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${LIB_DIR}/include
        ${LIB_DIR}/src
    )
    target_compile_options(${name} PRIVATE
        -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-sanitize-recover=all)
    target_link_options(${name} PRIVATE -fsanitize=address,undefined)
    target_link_libraries(${name} PRIVATE m)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_lib_test(test_port)
```

- [ ] **Step 2: 失敗するテストを書く**

`test/test_port.c`:

```c
#include "mor0koshi_port.h"
#include "test.h"

static void test_irq_save_restore_nests(void) {
    CHECK_EQ(fake.primask, 0);
    uint32_t outer = port_irq_save();
    CHECK_EQ(fake.primask, 1);
    uint32_t inner = port_irq_save(); /* 割り込みを止めた中でさらに止める */
    port_irq_restore(inner);
    CHECK_EQ(fake.primask, 1);        /* 内側を抜けてもまだ止まったまま */
    port_irq_restore(outer);
    CHECK_EQ(fake.primask, 0);
}

static void test_port_fail_calls_hook(void) {
    CHECK_EQ(port_fail(LIB_ERR_FULL, "test"), LIB_ERR_FULL);
    CHECK_EQ(fake.error_count, 1);
    CHECK_EQ(fake.last_error, LIB_ERR_FULL);
}

int main(void) {
    RUN(test_irq_save_restore_nests);
    RUN(test_port_fail_calls_hook);
    return TEST_RESULT();
}
```

- [ ] **Step 3: 失敗することを確かめる**

Run: (上の「テストの走らせ方」)
Expected: ビルドが失敗する (`mor0koshi_port.h: No such file or directory` または `mor0koshi_common.c` が無い)

- [ ] **Step 4: 共通の型と割り込みの補助を書く**

`include/mor0koshi_common.h`:

```c
#ifndef MOR0KOSHI_COMMON_H
#define MOR0KOSHI_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

/* 関数の結果。LIB_OK 以外のときは lib_error_hook も呼ばれている */
typedef enum {
    LIB_OK = 0,
    LIB_ERR_PARAM,   /* 引数がおかしい (ID が範囲外、同じ ID を二重に登録、NULL など) */
    LIB_ERR_FULL,    /* 登録の表、または送信待ちの列がいっぱい */
    LIB_ERR_HAL,     /* HAL の関数が失敗した */
    LIB_ERR_IRQ_OFF, /* CubeMX で必要な割り込みや DMA がオフ */
} lib_status_t;

/*
 * ライブラリの中で何か失敗するたびに呼ばれる。中身は空 (weak)。
 * 使う側が同じ名前で書けば、printf などで原因が分かる。where は関数名。
 * CAN が抜けているときなどは 1ms ごとに呼ばれることもあるので、重い処理は書かないこと。
 */
void lib_error_hook(lib_status_t err, const char *where);

#ifdef __cplusplus
}
#endif

#endif
```

`src/mor0koshi_common.c`:

```c
/* ライブラリ全体で使うもの */
#include "mor0koshi_common.h"

/* 使う側が同じ名前の関数を書けば、そちらが使われる */
__attribute__((weak)) void lib_error_hook(lib_status_t err, const char *where) {
    (void)err;
    (void)where;
}
```

`src/mor0koshi_port.h`:

```c
#ifndef MOR0KOSHI_PORT_H
#define MOR0KOSHI_PORT_H
/*
 * ライブラリの中だけで使う、HAL・CMSIS への入口。
 * テストでは main.h の代わりに test/fake/main.h が入る。
 */
#include <stdint.h>

#include "main.h"
#include "mor0koshi_common.h"

/* 割り込みを止める。戻り値を port_irq_restore に渡すと元の状態に戻る (入れ子にしても大丈夫) */
static inline uint32_t port_irq_save(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static inline void port_irq_restore(uint32_t primask) {
    __set_PRIMASK(primask);
}

/* lib_error_hook を呼んでから err を返す。失敗したときの return をこれ 1 行で書く */
static inline lib_status_t port_fail(lib_status_t err, const char *where) {
    lib_error_hook(err, where);
    return err;
}

#endif
```

`include/mor0koshi_library.h`:

```c
#ifndef MOR0KOSHI_LIBRARY_H
#define MOR0KOSHI_LIBRARY_H
/* これ 1 つを include すれば、ライブラリの全部の部品が使える */
#include "mor0koshi_common.h"

#endif
```

`CMakeLists.txt` (リポジトリ直下):

```cmake
cmake_minimum_required(VERSION 3.22)

# mor0koshi_library
#
# 使う側の CMakeLists.txt で:
#   add_subdirectory(../mor0koshi_library mor0koshi_library)
#   target_link_libraries(${CMAKE_PROJECT_NAME} stm32cubemx mor0koshi_library)
#
# 静的ライブラリなので、呼んだ部品の .c (と、そこにある HAL のコールバック) だけがリンクされる。

add_library(mor0koshi_library STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mor0koshi_common.c
)
target_include_directories(mor0koshi_library
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# 使う側のプロジェクトの HAL のインクルードパスとマクロ (STM32F767xx など) でコンパイルする
target_link_libraries(mor0koshi_library PUBLIC stm32cubemx)
target_compile_options(mor0koshi_library PRIVATE -Wall -Wextra -Wdouble-promotion)
```

- [ ] **Step 5: テストが通ることを確かめる**

Run: (上の「テストの走らせ方」)
Expected: `100% tests passed, 0 tests failed out of 1`

- [ ] **Step 6: コミット**

```bash
cd "/home/fukui-ros/STM32 project/mor0koshi_library"
git add .gitignore CMakeLists.txt include src test
git commit -F - <<'EOF'
ライブラリの土台と、PC で動くテストの枠組みを追加

共通のエラーの型 (lib_status_t / lib_error_hook)、割り込みの禁止・復帰の補助、
HAL の偽物 (test/fake) を用意した。

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task 2: CAN (`mor0koshi_can`)

**Files:**
- Create: `include/mor0koshi_can.h`、`src/mor0koshi_can.c`、`test/fake/fake_can.c`
- Modify: `CMakeLists.txt` (ソースを足す)、`include/mor0koshi_library.h` (include を足す)、`test/CMakeLists.txt` (ソースとテストを足す)
- Test: `test/test_can.c`

**Interfaces:**
- Consumes: `lib_status_t`、`lib_error_hook`、`port_irq_save` / `port_irq_restore` / `port_fail` (Task 1)
- Produces:
  - 型: `can_t`、`can_box_t` (メンバー `len`・`received`・`rx_ms` をテストから読む)、`typedef void (*can_rx_fn)(uint32_t id, const uint8_t *data, uint8_t len, void *arg);`
  - 定数: `CAN_MAX_SUBS` (16)、`CAN_TXQ_LEN` (16)、`CAN_MAX_BUSES` (3)
  - `lib_status_t can_init(can_t *bus, CAN_HandleTypeDef *hcan);`
  - `lib_status_t can_send(can_t *bus, uint32_t id, const uint8_t *data, uint8_t len);`
  - `lib_status_t can_listen(can_t *bus, uint32_t id, can_box_t *box);`
  - `bool can_read(can_box_t *box, uint8_t out[8]);` / `bool can_peek(const can_box_t *box, uint8_t out[8]);`
  - `uint32_t can_age_ms(const can_box_t *box);` / `uint32_t can_bus_age_ms(const can_t *bus);` / `uint32_t can_dropped_count(const can_t *bus);`
  - `lib_status_t can_subscribe(can_t *bus, uint32_t id, can_rx_fn fn, void *arg);`
  - `lib_status_t can_subscribe_range(can_t *bus, uint32_t first, uint32_t last, can_rx_fn fn, void *arg);`
  - `can_put_u8/i8/u16/i16/u32/i32/float(uint8_t *d, int offset, 値)`、`can_get_u8/i8/u16/i16/u32/i32/float(const uint8_t *d, int offset)` (リトルエンディアン)

- [ ] **Step 1: 偽物から CAN の割り込みを起こす補助を書き、ビルドに足す**

`test/fake/fake_can.c`:

```c
/* 偽物から CAN の割り込みを起こす補助。ライブラリの CAN のコールバックを呼ぶので、Task 2 から使う */
#include <string.h>

#include "fake_hal.h"

void fake_can_receive(CAN_HandleTypeDef *hcan, uint32_t id, const uint8_t *data, uint8_t len) {
    if (fake.can_rxq_count >= FAKE_MAX_RXQ) {
        return;
    }
    CAN_RxHeaderTypeDef *h = &fake.can_rxq_header[fake.can_rxq_count];
    memset(h, 0, sizeof *h);
    h->StdId = id;
    h->IDE = CAN_ID_STD;
    h->RTR = CAN_RTR_DATA;
    h->DLC = len;
    memset(fake.can_rxq_data[fake.can_rxq_count], 0xEE, 8); /* len より後ろはゴミ (ライブラリが 0 にするか確かめる) */
    memcpy(fake.can_rxq_data[fake.can_rxq_count], data, len);
    fake.can_rxq_count++;
    HAL_CAN_RxFifo0MsgPendingCallback(hcan);
}

void fake_can_tx_complete(CAN_HandleTypeDef *hcan) {
    if (fake.can_free_mailboxes < 3) {
        fake.can_free_mailboxes++;
    }
    HAL_CAN_TxMailbox0CompleteCallback(hcan);
}
```

`test/CMakeLists.txt` の `LIB_SOURCES` を次のようにし、最後に `add_lib_test(test_can)` を足す:

```cmake
set(LIB_SOURCES
    ${LIB_DIR}/src/mor0koshi_common.c
    ${LIB_DIR}/src/mor0koshi_can.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fake/fake_hal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fake/fake_can.c
)
```

```cmake
add_lib_test(test_port)
add_lib_test(test_can)
```

- [ ] **Step 2: 失敗するテストを書く**

`test/test_can.c`:

```c
#include "mor0koshi_can.h"
#include "test.h"

static CAN_HandleTypeDef hcan1 = {.Instance = CAN1};
static CAN_HandleTypeDef hcan2 = {.Instance = CAN2};
static can_t can1;
static can_t can2;
static can_box_t box;

static void test_put_get_roundtrip(void) {
    uint8_t d[8] = {0};
    can_put_i16(d, 0, -1234);
    CHECK_EQ(d[0], 0x2E); /* -1234 = 0xFB2E。リトルエンディアンなので下位が先 */
    CHECK_EQ(d[1], 0xFB);
    CHECK_EQ(can_get_i16(d, 0), -1234);
    can_put_u16(d, 2, 0xBEEF);
    CHECK_EQ(can_get_u16(d, 2), 0xBEEF);
    can_put_i32(d, 4, -100000);
    CHECK_EQ(can_get_i32(d, 4), -100000);
    can_put_u32(d, 4, 0x12345678);
    CHECK_EQ(d[4], 0x78);
    CHECK_EQ(can_get_u32(d, 4), 0x12345678);
    can_put_float(d, 0, 3.5f);
    CHECK_NEAR(can_get_float(d, 0), 3.5, 1e-9);
    can_put_i8(d, 0, -5);
    CHECK_EQ(can_get_i8(d, 0), -5);
    can_put_u8(d, 1, 200);
    CHECK_EQ(can_get_u8(d, 1), 200);
}

static void test_init_configures_hardware(void) {
    CHECK_EQ(can_init(&can1, &hcan1), LIB_OK);
    CHECK_EQ(fake.can_filter_calls, 1);
    CHECK_EQ(fake.can_last_filter.FilterBank, 0);
    CHECK_EQ(fake.can_last_filter.FilterMaskIdHigh, 0); /* マスク 0 = 全部受け取る */
    CHECK_EQ(fake.can_last_filter.FilterMaskIdLow, 0);
    CHECK_EQ(fake.can_last_filter.FilterFIFOAssignment, CAN_FILTER_FIFO0);
    CHECK_EQ(fake.can_last_filter.FilterActivation, ENABLE);
    CHECK_EQ(fake.can_start_calls, 1);
    CHECK(fake.can_mcr_at_start & CAN_MCR_ABOM); /* Start の前に ABOM が立っている */
    CHECK(fake.can_notifications & CAN_IT_RX_FIFO0_MSG_PENDING);
    CHECK(fake.can_notifications & CAN_IT_TX_MAILBOX_EMPTY);
    CHECK_EQ(fake.primask, 0);
}

static void test_init_can2_uses_filter_bank_14(void) {
    CHECK_EQ(can_init(&can2, &hcan2), LIB_OK);
    CHECK_EQ(fake.can_last_filter.FilterBank, 14);
    CHECK_EQ(fake.can_last_filter.SlaveStartFilterBank, 14);
}

static void test_init_fails_when_irq_off(void) {
    fake.nvic_enabled[CAN1_TX_IRQn] = 0;
    CHECK_EQ(can_init(&can1, &hcan1), LIB_ERR_IRQ_OFF);
    CHECK_EQ(fake.last_error, LIB_ERR_IRQ_OFF);
    CHECK_EQ(fake.can_start_calls, 0);
}

static void test_init_reports_hal_error(void) {
    fake.can_start_ret = HAL_ERROR;
    CHECK_EQ(can_init(&can1, &hcan1), LIB_ERR_HAL);
    CHECK_EQ(fake.last_error, LIB_ERR_HAL);
}

static void test_listen_and_read(void) {
    uint8_t out[8];
    can_init(&can1, &hcan1);
    CHECK_EQ(can_listen(&can1, 0x123, &box), LIB_OK);
    CHECK(!can_read(&box, out)); /* まだ何も来ていない */
    CHECK(!can_peek(&box, out));
    CHECK_EQ(can_age_ms(&box), UINT32_MAX);

    fake.tick = 1000;
    uint8_t d[3] = {1, 2, 3};
    fake_can_receive(&hcan1, 0x123, d, 3);
    fake.tick = 1040;
    CHECK(can_read(&box, out));
    CHECK_EQ(out[0], 1);
    CHECK_EQ(out[2], 3);
    CHECK_EQ(out[3], 0); /* len より後ろは 0 */
    CHECK_EQ(box.len, 3);
    CHECK(!can_read(&box, out)); /* 一度読んだら、次が届くまで false */
    CHECK_EQ(can_age_ms(&box), 40);
    CHECK(can_peek(&box, out)); /* peek は新しいかどうかに関係なく読める */
    CHECK_EQ(out[1], 2);
    CHECK_EQ(fake.primask, 0);
}

static void test_unknown_id_is_ignored(void) {
    uint8_t out[8];
    can_init(&can1, &hcan1);
    can_listen(&can1, 0x123, &box);
    CHECK_EQ(can_bus_age_ms(&can1), UINT32_MAX);
    uint8_t d[1] = {9};
    fake.tick = 50;
    fake_can_receive(&hcan1, 0x124, d, 1);
    CHECK(!can_read(&box, out));
    CHECK_EQ(can_bus_age_ms(&can1), 0); /* 知らない ID でも、バスとしては受信している */
}

static int calls_a, calls_b;
static uint32_t last_id_a;

static void handler_a(uint32_t id, const uint8_t *data, uint8_t len, void *arg) {
    (void)data;
    (void)len;
    calls_a++;
    last_id_a = id;
    CHECK(arg == &calls_a);
}

static void handler_b(uint32_t id, const uint8_t *data, uint8_t len, void *arg) {
    (void)id;
    (void)data;
    (void)len;
    (void)arg;
    calls_b++;
}

static void test_subscribe_range_and_overlap(void) {
    calls_a = calls_b = 0;
    can_init(&can1, &hcan1);
    CHECK_EQ(can_subscribe_range(&can1, 0x201, 0x208, handler_a, &calls_a), LIB_OK);
    CHECK_EQ(can_subscribe(&can1, 0x205, handler_b, NULL), LIB_OK);
    uint8_t d[8] = {0};
    fake_can_receive(&hcan1, 0x201, d, 8);
    fake_can_receive(&hcan1, 0x205, d, 8); /* 範囲と単独の両方に当たる */
    fake_can_receive(&hcan1, 0x209, d, 8); /* どちらにも当たらない */
    CHECK_EQ(calls_a, 2);
    CHECK_EQ(last_id_a, 0x205);
    CHECK_EQ(calls_b, 1);
}

static void test_subscribe_rejects_bad_args_and_full_table(void) {
    can_init(&can1, &hcan1);
    CHECK_EQ(can_subscribe_range(&can1, 0x300, 0x200, handler_b, NULL), LIB_ERR_PARAM);
    CHECK_EQ(can_subscribe(&can1, 0x800, handler_b, NULL), LIB_ERR_PARAM); /* 標準 ID は 0x7FF まで */
    CHECK_EQ(can_subscribe(&can1, 0x100, NULL, NULL), LIB_ERR_PARAM);
    for (int i = 0; i < CAN_MAX_SUBS; i++) {
        CHECK_EQ(can_subscribe(&can1, (uint32_t)i, handler_b, NULL), LIB_OK);
    }
    CHECK_EQ(can_subscribe(&can1, 0x100, handler_b, NULL), LIB_ERR_FULL);
    CHECK_EQ(fake.last_error, LIB_ERR_FULL);
}

static void test_send_goes_to_mailbox(void) {
    can_init(&can1, &hcan1);
    uint8_t d[2] = {0xAA, 0xBB};
    CHECK_EQ(can_send(&can1, 0x321, d, 2), LIB_OK);
    CHECK_EQ(fake.can_sent_count, 1);
    CHECK_EQ(fake.can_sent[0].header.StdId, 0x321);
    CHECK_EQ(fake.can_sent[0].header.IDE, CAN_ID_STD);
    CHECK_EQ(fake.can_sent[0].header.RTR, CAN_RTR_DATA);
    CHECK_EQ(fake.can_sent[0].header.DLC, 2);
    CHECK_EQ(fake.can_sent[0].data[1], 0xBB);
    CHECK_EQ(fake.primask, 0);
}

static void test_send_rejects_bad_args(void) {
    static can_t not_inited;
    uint8_t d[8] = {0};
    can_init(&can1, &hcan1);
    CHECK_EQ(can_send(&can1, 0x800, d, 8), LIB_ERR_PARAM);
    CHECK_EQ(can_send(&can1, 0x100, d, 9), LIB_ERR_PARAM);
    CHECK_EQ(can_send(&can1, 0x100, NULL, 1), LIB_ERR_PARAM);
    CHECK_EQ(can_send(&not_inited, 0x100, d, 8), LIB_ERR_PARAM); /* can_init していない */
    CHECK_EQ(fake.can_sent_count, 0);
}

static void test_send_queues_when_mailboxes_full(void) {
    can_init(&can1, &hcan1);
    uint8_t d[1];
    for (int i = 0; i < 5; i++) {
        d[0] = (uint8_t)i;
        CHECK_EQ(can_send(&can1, 0x100 + (uint32_t)i, d, 1), LIB_OK);
    }
    CHECK_EQ(fake.can_sent_count, 3); /* メールボックスは 3 つ。残り 2 つは列で待つ */
    fake_can_tx_complete(&hcan1);
    CHECK_EQ(fake.can_sent_count, 4);
    fake_can_tx_complete(&hcan1);
    CHECK_EQ(fake.can_sent_count, 5);
    CHECK_EQ(fake.can_sent[3].header.StdId, 0x103); /* 積んだ順に出る */
    CHECK_EQ(fake.can_sent[4].header.StdId, 0x104);
    fake_can_tx_complete(&hcan1); /* 列が空なら何もしない */
    CHECK_EQ(fake.can_sent_count, 5);
}

static void test_send_drops_when_queue_full(void) {
    can_init(&can1, &hcan1);
    fake.can_free_mailboxes = 0; /* ケーブルが抜けて送れない状態 */
    uint8_t d[1] = {0};
    for (int i = 0; i < CAN_TXQ_LEN; i++) {
        CHECK_EQ(can_send(&can1, 0x100, d, 1), LIB_OK);
    }
    CHECK_EQ(can_send(&can1, 0x100, d, 1), LIB_ERR_FULL);
    CHECK_EQ(can_dropped_count(&can1), 1);
    CHECK_EQ(fake.last_error, LIB_ERR_FULL);
    CHECK_EQ(fake.can_sent_count, 0);
    CHECK_EQ(fake.primask, 0);
}

static void test_null_args_do_nothing(void) {
    uint8_t d[8];
    CHECK_EQ(can_init(NULL, &hcan1), LIB_ERR_PARAM);
    CHECK_EQ(can_init(&can1, NULL), LIB_ERR_PARAM);
    CHECK_EQ(can_listen(&can1, 0x10, NULL), LIB_ERR_PARAM);
    CHECK(!can_read(NULL, d));
    CHECK(!can_peek(NULL, d));
    CHECK_EQ(can_age_ms(NULL), UINT32_MAX);
    CHECK_EQ(can_bus_age_ms(NULL), UINT32_MAX);
    CHECK_EQ(can_dropped_count(NULL), 0);
}

int main(void) {
    RUN(test_put_get_roundtrip);
    RUN(test_init_configures_hardware);
    RUN(test_init_can2_uses_filter_bank_14);
    RUN(test_init_fails_when_irq_off);
    RUN(test_init_reports_hal_error);
    RUN(test_listen_and_read);
    RUN(test_unknown_id_is_ignored);
    RUN(test_subscribe_range_and_overlap);
    RUN(test_subscribe_rejects_bad_args_and_full_table);
    RUN(test_send_goes_to_mailbox);
    RUN(test_send_rejects_bad_args);
    RUN(test_send_queues_when_mailboxes_full);
    RUN(test_send_drops_when_queue_full);
    RUN(test_null_args_do_nothing);
    return TEST_RESULT();
}
```

- [ ] **Step 3: 失敗することを確かめる**

Run: (テストの走らせ方)
Expected: CMake の設定かビルドが失敗する (`mor0koshi_can.c` / `mor0koshi_can.h` が無い)

- [ ] **Step 4: CAN を実装する**

`include/mor0koshi_can.h`:

```c
#ifndef MOR0KOSHI_CAN_H
#define MOR0KOSHI_CAN_H
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
 */
#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "mor0koshi_common.h"

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
```

`src/mor0koshi_can.c`:

```c
/*
 * CAN の送受信。
 * 受信: フィルタは全部受け取る設定にして、ID を見て箱 (can_listen) や関数 (can_subscribe) に配る。
 * 送信: メールボックスは 3 つしかないので、16 フレームの列に積んで、送信完了の割り込みで順に出す。
 */
#include "mor0koshi_can.h"

#include <string.h>

#include "mor0koshi_port.h"

/* ---- 詰める・取り出す (CAN が無効なプロジェクトでも使える) ---- */

void can_put_u8(uint8_t *d, int offset, uint8_t v) { d[offset] = v; }
void can_put_i8(uint8_t *d, int offset, int8_t v) { d[offset] = (uint8_t)v; }

void can_put_u16(uint8_t *d, int offset, uint16_t v) {
    d[offset] = (uint8_t)v;
    d[offset + 1] = (uint8_t)(v >> 8);
}

void can_put_i16(uint8_t *d, int offset, int16_t v) { can_put_u16(d, offset, (uint16_t)v); }

void can_put_u32(uint8_t *d, int offset, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        d[offset + i] = (uint8_t)(v >> (8 * i));
    }
}

void can_put_i32(uint8_t *d, int offset, int32_t v) { can_put_u32(d, offset, (uint32_t)v); }

void can_put_float(uint8_t *d, int offset, float v) {
    uint32_t u;
    memcpy(&u, &v, sizeof u);
    can_put_u32(d, offset, u);
}

uint8_t can_get_u8(const uint8_t *d, int offset) { return d[offset]; }
int8_t can_get_i8(const uint8_t *d, int offset) { return (int8_t)d[offset]; }

uint16_t can_get_u16(const uint8_t *d, int offset) {
    return (uint16_t)(d[offset] | (d[offset + 1] << 8));
}

int16_t can_get_i16(const uint8_t *d, int offset) { return (int16_t)can_get_u16(d, offset); }

uint32_t can_get_u32(const uint8_t *d, int offset) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        v |= (uint32_t)d[offset + i] << (8 * i);
    }
    return v;
}

int32_t can_get_i32(const uint8_t *d, int offset) { return (int32_t)can_get_u32(d, offset); }

float can_get_float(const uint8_t *d, int offset) {
    uint32_t u = can_get_u32(d, offset);
    float v;
    memcpy(&v, &u, sizeof v);
    return v;
}

#if defined(HAL_CAN_MODULE_ENABLED)

/* HAL のコールバックは hcan しかくれないので、どの can_t かをここで探す */
static can_t *buses[CAN_MAX_BUSES];

static can_t *find_bus(const CAN_HandleTypeDef *hcan) {
    for (int i = 0; i < CAN_MAX_BUSES; i++) {
        if (buses[i] != NULL && buses[i]->hcan == hcan) {
            return buses[i];
        }
    }
    return NULL;
}

/* 同じ bus か同じ hcan が登録済みならそこを上書きし、無ければ空きに入れる。割り込みを止めて呼ぶこと */
static bool register_bus(can_t *bus) {
    int slot = -1;
    for (int i = 0; i < CAN_MAX_BUSES && slot < 0; i++) {
        if (buses[i] == bus || (buses[i] != NULL && buses[i]->hcan == bus->hcan)) {
            slot = i;
        }
    }
    for (int i = 0; i < CAN_MAX_BUSES && slot < 0; i++) {
        if (buses[i] == NULL) {
            slot = i;
        }
    }
    if (slot < 0) {
        return false;
    }
    buses[slot] = bus;
    return true;
}

/* CubeMX の NVIC で RX0 と TX の割り込みがオンになっているか */
static bool irqs_enabled(const CAN_HandleTypeDef *hcan) {
    IRQn_Type rx;
    IRQn_Type tx;
    if (hcan->Instance == CAN1) {
        rx = CAN1_RX0_IRQn;
        tx = CAN1_TX_IRQn;
    }
#if defined(CAN2)
    else if (hcan->Instance == CAN2) {
        rx = CAN2_RX0_IRQn;
        tx = CAN2_TX_IRQn;
    }
#endif
#if defined(CAN3)
    else if (hcan->Instance == CAN3) {
        rx = CAN3_RX0_IRQn;
        tx = CAN3_TX_IRQn;
    }
#endif
    else {
        return false;
    }
    return NVIC_GetEnableIRQ(rx) != 0U && NVIC_GetEnableIRQ(tx) != 0U;
}

lib_status_t can_init(can_t *bus, CAN_HandleTypeDef *hcan) {
    if (bus == NULL || hcan == NULL) {
        return port_fail(LIB_ERR_PARAM, "can_init");
    }
    if (!irqs_enabled(hcan)) {
        return port_fail(LIB_ERR_IRQ_OFF, "can_init");
    }

    uint32_t pm = port_irq_save();
    memset(bus, 0, sizeof *bus);
    bus->hcan = hcan;
    bool registered = register_bus(bus);
    port_irq_restore(pm);
    if (!registered) {
        return port_fail(LIB_ERR_FULL, "can_init");
    }

    /* マスク 0 = 全部受け取る。ID の振り分けはソフトでやる */
    CAN_FilterTypeDef f = {0};
    f.FilterMode = CAN_FILTERMODE_IDMASK;
    f.FilterScale = CAN_FILTERSCALE_32BIT;
    f.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    f.FilterActivation = ENABLE;
    f.SlaveStartFilterBank = 14; /* CAN1 と CAN2 はフィルタを共有する。0〜13 が CAN1、14〜27 が CAN2 */
    f.FilterBank = 0;
#if defined(CAN2)
    if (hcan->Instance == CAN2) {
        f.FilterBank = 14;
    }
#endif
    if (HAL_CAN_ConfigFilter(hcan, &f) != HAL_OK) {
        return port_fail(LIB_ERR_HAL, "can_init");
    }

    /* バスオフ (ケーブル抜けなど) から自動で復帰させる。HAL_CAN_Start の前 (初期化モードの間) に立てる */
    hcan->Instance->MCR |= CAN_MCR_ABOM;

    if (HAL_CAN_Start(hcan) != HAL_OK) {
        return port_fail(LIB_ERR_HAL, "can_init");
    }
    if (HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_TX_MAILBOX_EMPTY) != HAL_OK) {
        return port_fail(LIB_ERR_HAL, "can_init");
    }
    return LIB_OK;
}

/* ---- 受信 ---- */

lib_status_t can_subscribe_range(can_t *bus, uint32_t first, uint32_t last, can_rx_fn fn, void *arg) {
    if (bus == NULL || fn == NULL || first > last || last > 0x7FFU) {
        return port_fail(LIB_ERR_PARAM, "can_subscribe");
    }
    uint32_t pm = port_irq_save();
    if (bus->nsubs >= CAN_MAX_SUBS) {
        port_irq_restore(pm);
        return port_fail(LIB_ERR_FULL, "can_subscribe");
    }
    can_sub_t *s = &bus->subs[bus->nsubs];
    s->first = (uint16_t)first;
    s->last = (uint16_t)last;
    s->fn = fn;
    s->arg = arg;
    bus->nsubs++; /* 中身を書き終えてから数を増やす (割り込みが途中の登録を見ないように) */
    port_irq_restore(pm);
    return LIB_OK;
}

lib_status_t can_subscribe(can_t *bus, uint32_t id, can_rx_fn fn, void *arg) {
    return can_subscribe_range(bus, id, id, fn, arg);
}

/* can_listen の箱に写す (受信割り込みの中) */
static void box_on_rx(uint32_t id, const uint8_t *data, uint8_t len, void *arg) {
    (void)id;
    can_box_t *box = arg;
    for (int i = 0; i < 8; i++) {
        box->data[i] = data[i];
    }
    box->len = len;
    box->rx_ms = HAL_GetTick(); /* received より先に書く (読む側が古い時刻を見ないように) */
    box->received = true;
    box->fresh = true;
}

lib_status_t can_listen(can_t *bus, uint32_t id, can_box_t *box) {
    if (box == NULL) {
        return port_fail(LIB_ERR_PARAM, "can_listen");
    }
    memset(box, 0, sizeof *box);
    return can_subscribe(bus, id, box_on_rx, box);
}

bool can_read(can_box_t *box, uint8_t out[8]) {
    if (box == NULL || out == NULL) {
        return false;
    }
    /* 写している途中で次のフレームが来て、新旧が混ざらないようにする */
    uint32_t pm = port_irq_save();
    bool fresh = box->fresh;
    if (fresh) {
        for (int i = 0; i < 8; i++) {
            out[i] = box->data[i];
        }
        box->fresh = false;
    }
    port_irq_restore(pm);
    return fresh;
}

bool can_peek(const can_box_t *box, uint8_t out[8]) {
    if (box == NULL || out == NULL) {
        return false;
    }
    uint32_t pm = port_irq_save();
    bool received = box->received;
    for (int i = 0; i < 8; i++) {
        out[i] = box->data[i];
    }
    port_irq_restore(pm);
    return received;
}

uint32_t can_age_ms(const can_box_t *box) {
    if (box == NULL || !box->received) {
        return UINT32_MAX;
    }
    uint32_t rx_ms = box->rx_ms; /* 先に読む (後から HAL_GetTick を読めば、差が負にならない) */
    return HAL_GetTick() - rx_ms;
}

uint32_t can_bus_age_ms(const can_t *bus) {
    if (bus == NULL || !bus->any_rx) {
        return UINT32_MAX;
    }
    uint32_t rx_ms = bus->last_rx_ms;
    return HAL_GetTick() - rx_ms;
}

uint32_t can_dropped_count(const can_t *bus) {
    return bus == NULL ? 0U : bus->dropped;
}

/* FIFO0 にフレームが来たとき。溜まっている分を全部取り出して配る */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan) {
    can_t *bus = find_bus(hcan);
    CAN_RxHeaderTypeDef h;
    uint8_t d[8];
    while (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) > 0U) {
        if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &h, d) != HAL_OK) {
            break;
        }
        if (bus == NULL || h.IDE != CAN_ID_STD || h.RTR != CAN_RTR_DATA) {
            continue;
        }
        uint8_t len = h.DLC > 8U ? 8U : (uint8_t)h.DLC;
        memset(&d[len], 0, (size_t)(8U - len)); /* len より後ろは 0 にして渡す */
        bus->last_rx_ms = HAL_GetTick();
        bus->any_rx = true;
        for (int i = 0; i < bus->nsubs; i++) {
            const can_sub_t *s = &bus->subs[i];
            if (h.StdId >= (uint32_t)s->first && h.StdId <= (uint32_t)s->last) {
                s->fn(h.StdId, d, len, s->arg);
            }
        }
    }
}

/* ---- 送信 ---- */

/* 列の先頭から、空いているメールボックスに入るだけ入れる。割り込みを止めた状態で呼ぶこと */
static void tx_pump(can_t *bus) {
    while (bus->txq_count > 0U && HAL_CAN_GetTxMailboxesFreeLevel(bus->hcan) > 0U) {
        can_frame_t *f = &bus->txq[bus->txq_head];
        CAN_TxHeaderTypeDef h = {0};
        h.StdId = f->id;
        h.IDE = CAN_ID_STD;
        h.RTR = CAN_RTR_DATA;
        h.DLC = f->len;
        h.TransmitGlobalTime = DISABLE;
        uint32_t mailbox;
        if (HAL_CAN_AddTxMessage(bus->hcan, &h, f->data, &mailbox) != HAL_OK) {
            break;
        }
        bus->txq_head = (uint8_t)((bus->txq_head + 1U) % CAN_TXQ_LEN);
        bus->txq_count--;
    }
}

lib_status_t can_send(can_t *bus, uint32_t id, const uint8_t *data, uint8_t len) {
    if (bus == NULL || bus->hcan == NULL || id > 0x7FFU || len > 8U || (len > 0U && data == NULL)) {
        return port_fail(LIB_ERR_PARAM, "can_send");
    }
    lib_status_t st = LIB_OK;
    uint32_t pm = port_irq_save();
    if (bus->txq_count >= CAN_TXQ_LEN) {
        bus->dropped++;
        st = LIB_ERR_FULL;
    } else {
        /* いったん列の最後に積んでから出す (先に待っているフレームを追い越さないように) */
        can_frame_t *f = &bus->txq[(bus->txq_head + bus->txq_count) % CAN_TXQ_LEN];
        f->id = (uint16_t)id;
        f->len = len;
        memset(f->data, 0, sizeof f->data);
        if (len > 0U) {
            memcpy(f->data, data, len);
        }
        bus->txq_count++;
    }
    tx_pump(bus);
    port_irq_restore(pm);
    if (st != LIB_OK) {
        lib_error_hook(st, "can_send");
    }
    return st;
}

/* メールボックスが 1 つ空いたら、列の続きを出す */
static void on_tx_done(CAN_HandleTypeDef *hcan) {
    can_t *bus = find_bus(hcan);
    if (bus != NULL) {
        tx_pump(bus);
    }
}

void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef *hcan) { on_tx_done(hcan); }
void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef *hcan) { on_tx_done(hcan); }
void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef *hcan) { on_tx_done(hcan); }
void HAL_CAN_TxMailbox0AbortCallback(CAN_HandleTypeDef *hcan) { on_tx_done(hcan); }
void HAL_CAN_TxMailbox1AbortCallback(CAN_HandleTypeDef *hcan) { on_tx_done(hcan); }
void HAL_CAN_TxMailbox2AbortCallback(CAN_HandleTypeDef *hcan) { on_tx_done(hcan); }

#endif /* HAL_CAN_MODULE_ENABLED */
```

`CMakeLists.txt` の `add_library` のソースに 1 行足す:

```cmake
add_library(mor0koshi_library STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mor0koshi_common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mor0koshi_can.c
)
```

`include/mor0koshi_library.h` の `#include "mor0koshi_common.h"` の下に足す:

```c
#include "mor0koshi_can.h"
```

- [ ] **Step 5: テストが通ることを確かめる**

Run: (テストの走らせ方)
Expected: `100% tests passed, 0 tests failed out of 2`

- [ ] **Step 6: コミット**

```bash
cd "/home/fukui-ros/STM32 project/mor0koshi_library"
git add CMakeLists.txt include src test
git commit -F - <<'EOF'
CAN の送受信を追加

can_init だけでフィルタ・バスオフからの自動復帰・割り込みまで設定する。
受信は ID ごとに箱 (can_listen) か関数 (can_subscribe) に振り分け、
送信はメールボックスが空くまで 16 フレームの列で待たせる。

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task 3: SBUS (`mor0koshi_sbus`)

**Files:**
- Create: `include/mor0koshi_sbus.h`、`src/mor0koshi_sbus.c`
- Modify: `CMakeLists.txt`、`include/mor0koshi_library.h`、`test/CMakeLists.txt`
- Test: `test/test_sbus.c`

**Interfaces:**
- Consumes: `lib_status_t`、`lib_error_hook`、`port_irq_save` / `port_irq_restore` / `port_fail` (Task 1)
- Produces:
  - 型 `sbus_t` (メンバー `rxbuf[SBUS_RXBUF_LEN]`・`stick_center`・`deadband` をテストから読む)、定数 `SBUS_CHANNELS` (16)、`SBUS_TIMEOUT_MS` (200)、`SBUS_MAX_PORTS` (2)、`SBUS_RXBUF_LEN` (64)
  - `lib_status_t sbus_init(sbus_t *s, UART_HandleTypeDef *huart);`
  - `void sbus_update(sbus_t *s);` / `bool sbus_ok(const sbus_t *s);`
  - `int sbus_stick(const sbus_t *s, int ch);` / `int sbus_switch2(const sbus_t *s, int ch);` / `int sbus_switch3(const sbus_t *s, int ch);` / `int sbus_raw(const sbus_t *s, int ch);`
  - `void sbus_set_stick_range(sbus_t *s, int min, int center, int max);` / `void sbus_set_deadband(sbus_t *s, int deadband);`
  - `uint32_t sbus_frame_count(const sbus_t *s);` / `uint32_t sbus_error_count(const sbus_t *s);`
  - weak: `void uart_rx_event_hook(UART_HandleTypeDef *huart, uint16_t size);` / `void uart_error_hook(UART_HandleTypeDef *huart);`

- [ ] **Step 1: テストをビルドに足す**

`test/CMakeLists.txt` の `LIB_SOURCES` に `${LIB_DIR}/src/mor0koshi_sbus.c` を足し (`mor0koshi_can.c` の次の行)、最後に `add_lib_test(test_sbus)` を足す。

- [ ] **Step 2: 失敗するテストを書く**

`test/test_sbus.c`:

```c
#include <string.h>

#include "mor0koshi_sbus.h"
#include "test.h"

static DMA_HandleTypeDef hdma;
static UART_HandleTypeDef huart5 = {.hdmarx = &hdma};
static UART_HandleTypeDef huart_other = {.hdmarx = &hdma};
static sbus_t sbus;

/* 16ch × 11bit を SBUS のフレーム (25 バイト) に詰める (下位ビットから順に) */
static void pack_frame(const uint16_t ch[16], uint8_t flags, uint8_t f[25]) {
    memset(f, 0, 25);
    f[0] = 0x0F;
    int bit = 0;
    for (int c = 0; c < 16; c++) {
        for (int b = 0; b < 11; b++, bit++) {
            if (ch[c] & (1u << b)) {
                f[1 + bit / 8] |= (uint8_t)(1u << (bit % 8));
            }
        }
    }
    f[23] = flags;
}

/* DMA がバッファの offset バイト目からフレームを書き、UART がアイドルになった、という状況を作る */
static void receive_frame(const uint16_t ch[16], uint8_t flags, int offset) {
    uint8_t f[25];
    pack_frame(ch, flags, f);
    memcpy(&sbus.rxbuf[offset], f, 25);
    HAL_UARTEx_RxEventCallback(&huart5, (uint16_t)(offset + 25));
}

static void all_channels(uint16_t ch[16], uint16_t v) {
    for (int i = 0; i < 16; i++) {
        ch[i] = v;
    }
}

static void test_init_starts_dma(void) {
    hdma.ht_disabled = 0;
    CHECK_EQ(sbus_init(&sbus, &huart5), LIB_OK);
    CHECK_EQ(fake.uart_rx_start_calls, 1);
    CHECK(fake.uart_rx_buf == sbus.rxbuf);
    CHECK_EQ(fake.uart_rx_size, SBUS_RXBUF_LEN);
    CHECK_EQ(hdma.ht_disabled, 1); /* 半分受信の割り込みは切る */
    CHECK_EQ((uintptr_t)sbus.rxbuf % 32, 0); /* D キャッシュの都合で 32 バイト境界 */
    CHECK_EQ(fake.primask, 0);
}

static void test_init_without_dma_fails(void) {
    static UART_HandleTypeDef no_dma = {.hdmarx = NULL};
    static sbus_t other;
    CHECK_EQ(sbus_init(&other, &no_dma), LIB_ERR_IRQ_OFF);
    CHECK_EQ(fake.uart_rx_start_calls, 0);
}

static void test_init_reports_hal_error(void) {
    fake.uart_rx_ret = HAL_ERROR;
    CHECK_EQ(sbus_init(&sbus, &huart5), LIB_ERR_HAL);
}

static void test_decode_hand_made_frame(void) {
    sbus_init(&sbus, &huart5);
    sbus.rxbuf[0] = 0x0F;
    sbus.rxbuf[1] = 0x23;
    sbus.rxbuf[2] = 0x29; /* 下位 3bit (1) が ch0 の上位、上位 5bit (5) が ch1 の下位 */
    HAL_UARTEx_RxEventCallback(&huart5, 25);
    sbus_update(&sbus);
    CHECK_EQ(sbus_raw(&sbus, 0), 0x123);
    CHECK_EQ(sbus_raw(&sbus, 1), 5);
    CHECK_EQ(sbus_raw(&sbus, 2), 0);
    CHECK_EQ(sbus_frame_count(&sbus), 1);
    CHECK_EQ(fake.uart_rx_start_calls, 2); /* 受信のたびに DMA をかけ直す */
}

static void test_decode_all_channels(void) {
    uint16_t ch[16];
    for (int i = 0; i < 16; i++) {
        ch[i] = (uint16_t)(100 * i + 7);
    }
    sbus_init(&sbus, &huart5);
    receive_frame(ch, 0, 0);
    sbus_update(&sbus);
    for (int i = 0; i < 16; i++) {
        CHECK_EQ(sbus_raw(&sbus, i), 100 * i + 7);
    }
    CHECK_EQ(sbus_raw(&sbus, 16), 0); /* チャンネル番号が範囲外 */
}

static void test_skips_junk_before_header(void) {
    uint16_t ch[16];
    all_channels(ch, 1024);
    ch[3] = 1500;
    sbus_init(&sbus, &huart5);
    sbus.rxbuf[0] = 0xAA;
    sbus.rxbuf[1] = 0x00;
    sbus.rxbuf[2] = 0x55;
    receive_frame(ch, 0, 3);
    sbus_update(&sbus);
    CHECK_EQ(sbus_raw(&sbus, 3), 1500);
}

static void test_short_receive_is_ignored(void) {
    sbus_init(&sbus, &huart5);
    sbus.rxbuf[0] = 0x0F;
    HAL_UARTEx_RxEventCallback(&huart5, 10); /* 25 バイトに足りない */
    sbus_update(&sbus);
    CHECK_EQ(sbus_frame_count(&sbus), 0);
    CHECK(!sbus_ok(&sbus));
    CHECK_EQ(fake.uart_rx_start_calls, 2); /* 受信はかけ直す */
}

static void test_ok_judgement(void) {
    uint16_t ch[16];
    all_channels(ch, 1024);
    sbus_init(&sbus, &huart5);
    fake.tick = 5000;
    sbus_update(&sbus);
    CHECK(!sbus_ok(&sbus)); /* まだ 1 フレームも来ていない */

    receive_frame(ch, 0, 0);
    sbus_update(&sbus);
    CHECK(sbus_ok(&sbus));

    fake.tick = 5199;
    sbus_update(&sbus);
    CHECK(sbus_ok(&sbus)); /* 199ms ならまだ OK */
    fake.tick = 5200;
    sbus_update(&sbus);
    CHECK(!sbus_ok(&sbus)); /* 200ms 空いたら NG (コネクタ抜け) */

    receive_frame(ch, 0x08, 0); /* フェイルセーフ (送信機の電源 OFF) */
    sbus_update(&sbus);
    CHECK(!sbus_ok(&sbus));

    receive_frame(ch, 0x04, 0); /* フレーム落ちだけなら OK のまま (ノイズで立つ) */
    sbus_update(&sbus);
    CHECK(sbus_ok(&sbus));
    CHECK_EQ(fake.primask, 0);
}

static void test_values_change_only_on_update(void) {
    uint16_t ch[16];
    all_channels(ch, 1024);
    ch[0] = 1680;
    sbus_init(&sbus, &huart5);
    receive_frame(ch, 0, 0);
    CHECK_EQ(sbus_raw(&sbus, 0), 0); /* sbus_update を呼ぶまでは前の値 */
    sbus_update(&sbus);
    CHECK_EQ(sbus_raw(&sbus, 0), 1680);
}

static void test_stick_conversion(void) {
    uint16_t ch[16];
    all_channels(ch, 1024);
    ch[0] = 1680;
    ch[1] = 368;
    ch[2] = 1050;
    ch[3] = 1051;
    ch[4] = 998;
    ch[5] = 2000;
    ch[6] = 0;
    ch[7] = 1352;
    sbus_init(&sbus, &huart5);
    receive_frame(ch, 0, 0);
    sbus_update(&sbus);
    CHECK_EQ(sbus_stick(&sbus, 0), 1000);
    CHECK_EQ(sbus_stick(&sbus, 1), -1000);
    CHECK_EQ(sbus_stick(&sbus, 2), 0);  /* 中立 +26 までは 0 */
    CHECK_EQ(sbus_stick(&sbus, 3), 41); /* 27 * 1000 / 656 */
    CHECK_EQ(sbus_stick(&sbus, 4), 0);  /* 中立 -26 までは 0 */
    CHECK_EQ(sbus_stick(&sbus, 5), 1000); /* 範囲の外は頭打ち */
    CHECK_EQ(sbus_stick(&sbus, 6), -1000);
    CHECK_EQ(sbus_stick(&sbus, 7), 500); /* 328 * 1000 / 656 */
    CHECK_EQ(sbus_stick(&sbus, 16), 0);  /* チャンネル番号が範囲外 */
}

static void test_stick_asymmetric_range(void) {
    uint16_t ch[16];
    all_channels(ch, 1000);
    ch[0] = 1811;
    ch[1] = 172;
    ch[2] = 1405;
    sbus_init(&sbus, &huart5);
    sbus_set_stick_range(&sbus, 172, 1000, 1811); /* 中立の上下で幅が違う送信機 */
    sbus_set_deadband(&sbus, 0);
    receive_frame(ch, 0, 0);
    sbus_update(&sbus);
    CHECK_EQ(sbus_stick(&sbus, 0), 1000);
    CHECK_EQ(sbus_stick(&sbus, 1), -1000);
    CHECK_EQ(sbus_stick(&sbus, 3), 0);   /* 中立はちょうど 0 */
    CHECK_EQ(sbus_stick(&sbus, 2), 499); /* 405 * 1000 / 811 */
}

static void test_switches(void) {
    uint16_t ch[16];
    all_channels(ch, 1024);
    ch[0] = 1680;
    ch[1] = 368;
    ch[2] = 1353;
    ch[3] = 1352;
    ch[4] = 695;
    ch[5] = 696;
    sbus_init(&sbus, &huart5);
    receive_frame(ch, 0, 0);
    sbus_update(&sbus);
    CHECK_EQ(sbus_switch3(&sbus, 0), 1); /* 生の値が大きいほうが +1 */
    CHECK_EQ(sbus_switch3(&sbus, 1), -1);
    CHECK_EQ(sbus_switch3(&sbus, 6), 0); /* 中立 */
    CHECK_EQ(sbus_switch3(&sbus, 2), 1); /* 上の境目は (1024 + 1680) / 2 = 1352 */
    CHECK_EQ(sbus_switch3(&sbus, 3), 0);
    CHECK_EQ(sbus_switch3(&sbus, 4), -1); /* 下の境目は (368 + 1024) / 2 = 696 */
    CHECK_EQ(sbus_switch3(&sbus, 5), 0);
    CHECK_EQ(sbus_switch2(&sbus, 0), 1);
    CHECK_EQ(sbus_switch2(&sbus, 1), 0);
    CHECK_EQ(sbus_switch2(&sbus, 6), 0); /* 中立ちょうどは 0 */
}

static void test_values_are_zero_when_not_ok(void) {
    uint16_t ch[16];
    all_channels(ch, 1680);
    sbus_init(&sbus, &huart5);
    receive_frame(ch, 0x08, 0); /* フェイルセーフ */
    sbus_update(&sbus);
    CHECK(!sbus_ok(&sbus));
    CHECK_EQ(sbus_stick(&sbus, 0), 0);
    CHECK_EQ(sbus_switch2(&sbus, 0), 0);
    CHECK_EQ(sbus_switch3(&sbus, 0), 0);
    CHECK_EQ(sbus_raw(&sbus, 0), 1680); /* 生の値はそのまま */
}

static int hook_events, hook_errors;

/* ライブラリの weak な hook をテスト側で上書きして、呼ばれたことを数える */
void uart_rx_event_hook(UART_HandleTypeDef *huart, uint16_t size) {
    (void)huart;
    (void)size;
    hook_events++;
}

void uart_error_hook(UART_HandleTypeDef *huart) {
    (void)huart;
    hook_errors++;
}

static void test_error_restarts_rx(void) {
    sbus_init(&sbus, &huart5);
    HAL_UART_ErrorCallback(&huart5);
    CHECK_EQ(fake.uart_rx_start_calls, 2);
    CHECK_EQ(sbus_error_count(&sbus), 1);
}

static void test_other_uart_goes_to_hooks(void) {
    hook_events = hook_errors = 0;
    sbus_init(&sbus, &huart5);
    HAL_UARTEx_RxEventCallback(&huart_other, 10);
    HAL_UART_ErrorCallback(&huart_other);
    CHECK_EQ(hook_events, 1);
    CHECK_EQ(hook_errors, 1);
    CHECK_EQ(fake.uart_rx_start_calls, 1); /* SBUS のほうは再開しない */
}

static void test_bad_settings_are_rejected(void) {
    sbus_init(&sbus, &huart5);
    sbus_set_stick_range(&sbus, 1000, 500, 1600);
    CHECK_EQ(fake.last_error, LIB_ERR_PARAM);
    CHECK_EQ(sbus.stick_center, 1024); /* 変わっていない */
    sbus_set_deadband(&sbus, -1);
    CHECK_EQ(sbus.deadband, 26);
    CHECK_EQ(sbus_init(NULL, &huart5), LIB_ERR_PARAM);
    sbus_update(NULL);
    CHECK(!sbus_ok(NULL));
    CHECK_EQ(sbus_stick(NULL, 0), 0);
    CHECK_EQ(sbus_raw(NULL, 0), 0);
    CHECK_EQ(sbus_frame_count(NULL), 0);
}

int main(void) {
    RUN(test_init_starts_dma);
    RUN(test_init_without_dma_fails);
    RUN(test_init_reports_hal_error);
    RUN(test_decode_hand_made_frame);
    RUN(test_decode_all_channels);
    RUN(test_skips_junk_before_header);
    RUN(test_short_receive_is_ignored);
    RUN(test_ok_judgement);
    RUN(test_values_change_only_on_update);
    RUN(test_stick_conversion);
    RUN(test_stick_asymmetric_range);
    RUN(test_switches);
    RUN(test_values_are_zero_when_not_ok);
    RUN(test_error_restarts_rx);
    RUN(test_other_uart_goes_to_hooks);
    RUN(test_bad_settings_are_rejected);
    return TEST_RESULT();
}
```

- [ ] **Step 3: 失敗することを確かめる**

Run: (テストの走らせ方)
Expected: CMake の設定かビルドが失敗する (`mor0koshi_sbus.c` / `mor0koshi_sbus.h` が無い)

- [ ] **Step 4: SBUS を実装する**

`include/mor0koshi_sbus.h`:

```c
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
```

`src/mor0koshi_sbus.c`:

```c
/*
 * SBUS の受信。2026_B_767 の sbus_handler.c を元にしている (実機確認済みの判定をそのまま使う)。
 * UART がアイドルになる (1 フレーム受け終わる) たびに、バッファから 0x0F を探して 25 バイトをデコードする。
 */
#include "mor0koshi_sbus.h"

#include <string.h>

#include "mor0koshi_port.h"

#if defined(HAL_UART_MODULE_ENABLED)

#define SBUS_FRAME_LEN 25
#define SBUS_HEADER 0x0FU
#define SBUS_FLAG_FAILSAFE 0x08U /* 受信機が送信機を見失った (フレーム落ち 0x04 は判定に使わない) */

/* HAL のコールバックは huart しかくれないので、どの sbus_t かをここで探す */
static sbus_t *ports[SBUS_MAX_PORTS];

static sbus_t *find_port(const UART_HandleTypeDef *huart) {
    for (int i = 0; i < SBUS_MAX_PORTS; i++) {
        if (ports[i] != NULL && ports[i]->huart == huart) {
            return ports[i];
        }
    }
    return NULL;
}

/* 同じ s か同じ huart が登録済みならそこを上書きし、無ければ空きに入れる。割り込みを止めて呼ぶこと */
static bool register_port(sbus_t *s) {
    int slot = -1;
    for (int i = 0; i < SBUS_MAX_PORTS && slot < 0; i++) {
        if (ports[i] == s || (ports[i] != NULL && ports[i]->huart == s->huart)) {
            slot = i;
        }
    }
    for (int i = 0; i < SBUS_MAX_PORTS && slot < 0; i++) {
        if (ports[i] == NULL) {
            slot = i;
        }
    }
    if (slot < 0) {
        return false;
    }
    ports[slot] = s;
    return true;
}

/* DMA で受信を (再) 開始する。DMA は Normal モードなので、1 回受信するたびに呼び直す */
static HAL_StatusTypeDef start_rx(sbus_t *s) {
    HAL_StatusTypeDef st = HAL_UARTEx_ReceiveToIdle_DMA(s->huart, s->rxbuf, SBUS_RXBUF_LEN);
    __HAL_DMA_DISABLE_IT(s->huart->hdmarx, DMA_IT_HT); /* 半分受信の割り込みは要らない */
    return st;
}

lib_status_t sbus_init(sbus_t *s, UART_HandleTypeDef *huart) {
    if (s == NULL || huart == NULL) {
        return port_fail(LIB_ERR_PARAM, "sbus_init");
    }
    if (huart->hdmarx == NULL) {
        return port_fail(LIB_ERR_IRQ_OFF, "sbus_init"); /* CubeMX で RX の DMA が設定されていない */
    }
    uint32_t pm = port_irq_save();
    memset(s, 0, sizeof *s);
    s->huart = huart;
    s->stick_min = 368;
    s->stick_center = 1024;
    s->stick_max = 1680;
    s->deadband = 26;
    bool registered = register_port(s);
    port_irq_restore(pm);
    if (!registered) {
        return port_fail(LIB_ERR_FULL, "sbus_init");
    }
    if (start_rx(s) != HAL_OK) {
        return port_fail(LIB_ERR_HAL, "sbus_init");
    }
    return LIB_OK;
}

/* 25 バイト (先頭 0x0F) を 11bit × 16ch とフラグにデコードする (受信割り込みの中) */
static void decode(sbus_t *s, const uint8_t *f) {
    uint32_t bits = 0;
    int nbits = 0;
    int ch = 0;
    for (int i = 1; i <= 22; i++) {
        bits |= (uint32_t)f[i] << nbits;
        nbits += 8;
        while (nbits >= 11 && ch < SBUS_CHANNELS) {
            s->isr_ch[ch++] = (uint16_t)(bits & 0x07FFU);
            bits >>= 11;
            nbits -= 11;
        }
    }
    s->isr_flags = f[23];
    /*
     * 受信が途絶えたことを sbus_update が判定できるように時刻を残す。
     * チャンネルの値はフレームが来たときしか変わらないので、コネクタが抜けると
     * 古い値のまま固まり、それだけでは断線を判定できない。
     */
    s->isr_rx_ms = HAL_GetTick(); /* isr_received より先に書く */
    s->isr_received = true;
    s->frame_count++;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
    sbus_t *s = find_port(huart);
    if (s == NULL) {
        uart_rx_event_hook(huart, Size);
        return;
    }
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    /* D キャッシュが有効だと、DMA が書いた新しい値ではなくキャッシュの古い値を読んでしまう */
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        SCB_InvalidateDCache_by_Addr((uint32_t *)(void *)s->rxbuf, SBUS_RXBUF_LEN);
    }
#endif
    for (int i = 0; i + SBUS_FRAME_LEN <= (int)Size && i + SBUS_FRAME_LEN <= SBUS_RXBUF_LEN; i++) {
        if (s->rxbuf[i] == SBUS_HEADER) {
            decode(s, &s->rxbuf[i]);
            break;
        }
    }
    start_rx(s);
}

/*
 * UART の受信エラー (パリティ・フレーミング・ノイズ・オーバーラン) で呼ばれる。
 * DMA 受信中にエラーが起きると HAL は受信を打ち切り、RxEventCallback も呼ばない。
 * ここで再開しないと、リセットするまで受信が止まったままになる。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    sbus_t *s = find_port(huart);
    if (s == NULL) {
        uart_error_hook(huart);
        return;
    }
    s->error_count++;
    start_rx(s);
}

__attribute__((weak)) void uart_rx_event_hook(UART_HandleTypeDef *huart, uint16_t size) {
    (void)huart;
    (void)size;
}

__attribute__((weak)) void uart_error_hook(UART_HandleTypeDef *huart) {
    (void)huart;
}

void sbus_update(sbus_t *s) {
    if (s == NULL) {
        return;
    }
    /* 途中で受信割り込みが入って別のフレームの値が混ざらないよう、まとめて写す */
    uint32_t pm = port_irq_save();
    for (int i = 0; i < SBUS_CHANNELS; i++) {
        s->ch[i] = s->isr_ch[i];
    }
    uint8_t flags = s->isr_flags;
    uint32_t rx_ms = s->isr_rx_ms;
    bool received = s->isr_received;
    port_irq_restore(pm);

    uint32_t now = HAL_GetTick();
    s->ok = received && (now - rx_ms) < SBUS_TIMEOUT_MS && (flags & SBUS_FLAG_FAILSAFE) == 0U;
}

bool sbus_ok(const sbus_t *s) { return s != NULL && s->ok; }

static bool valid_ch(int ch) { return ch >= 0 && ch < SBUS_CHANNELS; }

int sbus_stick(const sbus_t *s, int ch) {
    if (s == NULL || !valid_ch(ch) || !s->ok) {
        return 0;
    }
    int d = (int)s->ch[ch] - s->stick_center;
    if (d >= -s->deadband && d <= s->deadband) {
        return 0;
    }
    /* 中立の上下で別々に変換する (上下で幅が違う送信機でも、中立が 0 になるように) */
    int span = d > 0 ? s->stick_max - s->stick_center : s->stick_center - s->stick_min;
    long v = (long)d * 1000L / span;
    if (v > 1000L) {
        v = 1000L;
    } else if (v < -1000L) {
        v = -1000L;
    }
    return (int)v;
}

int sbus_switch3(const sbus_t *s, int ch) {
    if (s == NULL || !valid_ch(ch) || !s->ok) {
        return 0;
    }
    int raw = s->ch[ch];
    if (raw > (s->stick_center + s->stick_max) / 2) {
        return 1;
    }
    if (raw < (s->stick_min + s->stick_center) / 2) {
        return -1;
    }
    return 0;
}

int sbus_switch2(const sbus_t *s, int ch) {
    if (s == NULL || !valid_ch(ch) || !s->ok) {
        return 0;
    }
    return s->ch[ch] > s->stick_center ? 1 : 0;
}

int sbus_raw(const sbus_t *s, int ch) {
    if (s == NULL || !valid_ch(ch)) {
        return 0;
    }
    return s->ch[ch];
}

void sbus_set_stick_range(sbus_t *s, int min, int center, int max) {
    if (s == NULL || !(min < center && center < max)) {
        lib_error_hook(LIB_ERR_PARAM, "sbus_set_stick_range");
        return;
    }
    s->stick_min = min;
    s->stick_center = center;
    s->stick_max = max;
}

void sbus_set_deadband(sbus_t *s, int deadband) {
    if (s == NULL || deadband < 0) {
        lib_error_hook(LIB_ERR_PARAM, "sbus_set_deadband");
        return;
    }
    s->deadband = deadband;
}

uint32_t sbus_frame_count(const sbus_t *s) { return s == NULL ? 0U : s->frame_count; }
uint32_t sbus_error_count(const sbus_t *s) { return s == NULL ? 0U : s->error_count; }

#endif /* HAL_UART_MODULE_ENABLED */
```

`CMakeLists.txt` の `add_library` のソースに `${CMAKE_CURRENT_SOURCE_DIR}/src/mor0koshi_sbus.c` を足す (`mor0koshi_can.c` の次の行)。

`include/mor0koshi_library.h` に足す:

```c
#include "mor0koshi_sbus.h"
```

- [ ] **Step 5: テストが通ることを確かめる**

Run: (テストの走らせ方)
Expected: `100% tests passed, 0 tests failed out of 3`

- [ ] **Step 6: コミット**

```bash
cd "/home/fukui-ros/STM32 project/mor0koshi_library"
git add CMakeLists.txt include src test
git commit -F - <<'EOF'
SBUS の受信を追加

2026_B_767 の判定 (未受信・200ms 途絶・フェイルセーフで NG、フレーム落ちは無視) を
そのまま使い、スティック (±1000) と 2 段・3 段スイッチの変換を付けた。
UART エラーで受信が止まったら自動で再開する。

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task 4: エンコーダー (`mor0koshi_encoder`)

**Files:**
- Create: `include/mor0koshi_encoder.h`、`src/mor0koshi_encoder.c`
- Modify: `src/mor0koshi_port.h` (DWT を足す)、`CMakeLists.txt`、`include/mor0koshi_library.h`、`test/CMakeLists.txt`
- Test: `test/test_encoder.c`

**Interfaces:**
- Consumes: `lib_status_t`、`port_fail` (Task 1)
- Produces:
  - `static inline void port_cycles_enable(void);` / `static inline uint32_t port_cycles(void);` (port.h)
  - 型 `encoder_t`、定数 `ENCODER_RPM_RESET_MS` (10000)
  - `lib_status_t encoder_init(encoder_t *e, TIM_HandleTypeDef *htim, int32_t counts_per_rev);`
  - `void encoder_set_reverse(encoder_t *e, bool reverse);` / `void encoder_update(encoder_t *e);` / `void encoder_reset(encoder_t *e);`
  - `int32_t encoder_count(const encoder_t *e);` / `int32_t encoder_delta(const encoder_t *e);` / `float encoder_rpm(const encoder_t *e);` / `float encoder_turns(const encoder_t *e);`

- [ ] **Step 1: テストをビルドに足す**

`test/CMakeLists.txt` の `LIB_SOURCES` に `${LIB_DIR}/src/mor0koshi_encoder.c` を足し (`mor0koshi_sbus.c` の次の行)、最後に `add_lib_test(test_encoder)` を足す。

- [ ] **Step 2: 失敗するテストを書く**

`test/test_encoder.c`:

```c
#include "mor0koshi_encoder.h"
#include "test.h"

static TIM_TypeDef tim16 = {.CNT = 0, .ARR = 0xFFFF};
static TIM_TypeDef tim32 = {.CNT = 0, .ARR = 0xFFFFFFFF};
static TIM_HandleTypeDef htim16 = {.Instance = &tim16};
static TIM_HandleTypeDef htim32 = {.Instance = &tim32};
static encoder_t enc;

static void test_init(void) {
    tim16.CNT = 500;
    CHECK_EQ(encoder_init(&enc, &htim16, 2048), LIB_OK);
    CHECK_EQ(fake.tim_start_calls, 1);
    CHECK(fake_coredebug.DEMCR & CoreDebug_DEMCR_TRCENA_Msk); /* DWT のサイクルカウンタを動かしている */
    CHECK(fake_dwt.CTRL & DWT_CTRL_CYCCNTENA_Msk);
    CHECK_EQ(encoder_count(&enc), 0); /* 起動時の位置が 0 */
    CHECK_EQ(encoder_init(&enc, &htim16, 0), LIB_ERR_PARAM);
    CHECK_EQ(encoder_init(&enc, NULL, 2048), LIB_ERR_PARAM);
    fake.tim_start_ret = HAL_ERROR;
    CHECK_EQ(encoder_init(&enc, &htim16, 2048), LIB_ERR_HAL);
    CHECK_EQ(fake.last_error, LIB_ERR_HAL);
}

static void test_counts_and_delta(void) {
    tim16.CNT = 1000;
    encoder_init(&enc, &htim16, 2048);
    tim16.CNT = 1100;
    encoder_update(&enc);
    CHECK_EQ(encoder_delta(&enc), 100);
    CHECK_EQ(encoder_count(&enc), 100);
    tim16.CNT = 1050;
    encoder_update(&enc);
    CHECK_EQ(encoder_delta(&enc), -50);
    CHECK_EQ(encoder_count(&enc), 50);
}

static void test_16bit_wrap(void) {
    tim16.CNT = 65530;
    encoder_init(&enc, &htim16, 2048);
    tim16.CNT = 10; /* 65535 → 0 をまたいで +16 */
    encoder_update(&enc);
    CHECK_EQ(encoder_delta(&enc), 16);
    tim16.CNT = 65530; /* 逆向きにまたいで -16 */
    encoder_update(&enc);
    CHECK_EQ(encoder_delta(&enc), -16);
    CHECK_EQ(encoder_count(&enc), 0);
}

static void test_32bit_wrap(void) {
    tim32.CNT = 0xFFFFFFF0u;
    encoder_init(&enc, &htim32, 2048);
    tim32.CNT = 0x10;
    encoder_update(&enc);
    CHECK_EQ(encoder_delta(&enc), 32);
    tim32.CNT = 0x7FFF0000u; /* 32bit なら、16bit の半周を超えて進んでも正しく数える */
    encoder_update(&enc);
    CHECK_EQ(encoder_delta(&enc), 0x7FFF0000 - 0x10);
}

static void test_reverse(void) {
    tim16.CNT = 0;
    encoder_init(&enc, &htim16, 2048);
    encoder_set_reverse(&enc, true);
    tim16.CNT = 300;
    encoder_update(&enc);
    CHECK_EQ(encoder_delta(&enc), -300);
    CHECK_EQ(encoder_count(&enc), -300);
}

static void test_rpm_and_turns(void) {
    tim16.CNT = 0;
    fake_dwt.CYCCNT = 0;
    encoder_init(&enc, &htim16, 1000); /* SystemCoreClock は 100MHz (fake_reset) */
    tim16.CNT = 100;
    fake_dwt.CYCCNT = 1000000; /* 10ms 後に 0.1 回転 = 10 回転/秒 = 600rpm */
    fake.tick = 10;
    encoder_update(&enc);
    CHECK_NEAR(encoder_rpm(&enc), 600.0, 0.01);
    CHECK_NEAR(encoder_turns(&enc), 0.1, 1e-6);
    tim16.CNT = 50; /* 5ms 後に 0.05 回転戻った = -600rpm */
    fake_dwt.CYCCNT = 1500000;
    fake.tick = 15;
    encoder_update(&enc);
    CHECK_NEAR(encoder_rpm(&enc), -600.0, 0.01);
}

static void test_rpm_across_cycle_counter_wrap(void) {
    tim16.CNT = 0;
    fake_dwt.CYCCNT = 0xFFFFFFFFu - 499999u; /* あと 500000 で一周する */
    encoder_init(&enc, &htim16, 1000);
    tim16.CNT = 100;
    fake_dwt.CYCCNT = 499999u; /* 一周またいで 1000000 サイクル (10ms) 後 */
    fake.tick = 10;
    encoder_update(&enc);
    CHECK_NEAR(encoder_rpm(&enc), 600.0, 0.01);
}

static void test_rpm_zero_after_long_gap(void) {
    tim16.CNT = 0;
    encoder_init(&enc, &htim16, 1000);
    tim16.CNT = 100;
    fake.tick = 10000; /* 10 秒空いた (DWT が一周しているかもしれない) */
    fake_dwt.CYCCNT = 12345;
    encoder_update(&enc);
    CHECK_NEAR(encoder_rpm(&enc), 0.0, 1e-9);
    CHECK_EQ(encoder_count(&enc), 100); /* カウントは数える */
}

static void test_reset(void) {
    tim16.CNT = 0;
    encoder_init(&enc, &htim16, 1000);
    tim16.CNT = 700;
    encoder_update(&enc);
    encoder_reset(&enc);
    CHECK_EQ(encoder_count(&enc), 0);
    tim16.CNT = 710;
    encoder_update(&enc);
    CHECK_EQ(encoder_count(&enc), 10);
}

static void test_null_is_safe(void) {
    encoder_update(NULL);
    encoder_reset(NULL);
    encoder_set_reverse(NULL, true);
    CHECK_EQ(encoder_count(NULL), 0);
    CHECK_EQ(encoder_delta(NULL), 0);
    CHECK_NEAR(encoder_rpm(NULL), 0.0, 1e-9);
    CHECK_NEAR(encoder_turns(NULL), 0.0, 1e-9);
    CHECK_EQ(encoder_init(NULL, &htim16, 1000), LIB_ERR_PARAM);
}

int main(void) {
    RUN(test_init);
    RUN(test_counts_and_delta);
    RUN(test_16bit_wrap);
    RUN(test_32bit_wrap);
    RUN(test_reverse);
    RUN(test_rpm_and_turns);
    RUN(test_rpm_across_cycle_counter_wrap);
    RUN(test_rpm_zero_after_long_gap);
    RUN(test_reset);
    RUN(test_null_is_safe);
    return TEST_RESULT();
}
```

`test_rpm_across_cycle_counter_wrap` の計算: 開始 `0xFFFFFFFF - 499999` から一周の終わりまで 500000、そこから `499999` までで 500000、合わせて 1000000 サイクル (100MHz で 10ms)。

- [ ] **Step 3: 失敗することを確かめる**

Run: (テストの走らせ方)
Expected: CMake の設定かビルドが失敗する (`mor0koshi_encoder.c` / `mor0koshi_encoder.h` が無い)

- [ ] **Step 4: エンコーダーを実装する**

`src/mor0koshi_port.h` の `port_fail` の後ろ (`#endif` の前) に足す:

```c
/* DWT のサイクルカウンタを動かす (エンコーダーの rpm で、update を呼んだ間隔を測るのに使う) */
static inline void port_cycles_enable(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
#if defined(__CORTEX_M) && (__CORTEX_M == 7U)
    DWT->LAR = 0xC5ACCE55U; /* M7 はロックを外さないと書けない */
#endif
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* CPU のクロックで 1 ずつ増える。216MHz なら約 19.8 秒で一周する */
static inline uint32_t port_cycles(void) {
    return DWT->CYCCNT;
}
```

`include/mor0koshi_encoder.h`:

```c
#ifndef MOR0KOSHI_ENCODER_H
#define MOR0KOSHI_ENCODER_H
/*
 * タイマーのエンコーダーモードで回転を数える。CAN とは関係なく、その基板の中でそのまま使える。
 *
 *   encoder_t enc1;
 *   encoder_init(&enc1, &htim1, 2048);   // 2048 = 1 回転のカウント数 (4 逓倍込み)
 *   // 決まった周期 (1〜10ms くらい) で呼ぶ
 *   encoder_update(&enc1);
 *   float rpm = encoder_rpm(&enc1);
 *
 * CubeMX の準備: タイマーを Encoder Mode (TI1 and TI2)、Prescaler 0、Period を最大
 * (16bit なら 65535、TIM2 / TIM5 なら 0xFFFFFFFF) にする。割り込みは要らない。
 *
 * 遅い回転を 1ms 周期で測ると粗い (2048 カウントなら 1 カウント ≒ 29rpm)。その場合は 10ms 周期くらいにする。
 * encoder_update と値の読み出しは同じ場所 (メインループ同士、または割り込み同士) で行うこと。
 */
#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "mor0koshi_common.h"

#if defined(HAL_TIM_MODULE_ENABLED)

#ifdef __cplusplus
extern "C" {
#endif

#define ENCODER_RPM_RESET_MS 10000 /* update の間隔がこれ以上空いたら rpm を 0 にする (DWT が一周するため) */

typedef struct encoder {
    TIM_HandleTypeDef *htim;
    int32_t cpr; /* 1 回転のカウント数 (4 逓倍込み) */
    bool reverse;
    uint32_t last_cnt;    /* 前回のタイマーの値 */
    uint32_t last_cycles; /* 前回の DWT の値 */
    uint32_t last_ms;     /* 前回の HAL_GetTick */
    int32_t count;
    int32_t delta;
    float rpm;
} encoder_t;

lib_status_t encoder_init(encoder_t *e, TIM_HandleTypeDef *htim, int32_t counts_per_rev);
/* 向きを逆にする (これから先の変化量の符号が変わる) */
void encoder_set_reverse(encoder_t *e, bool reverse);
/* タイマーを読んで、累積カウント・変化量・rpm を更新する */
void encoder_update(encoder_t *e);

int32_t encoder_count(const encoder_t *e); /* 起動 (または reset) からの累積カウント */
int32_t encoder_delta(const encoder_t *e); /* 前回の update からの変化量 */
float encoder_rpm(const encoder_t *e);     /* 回転数 (符号付き) */
float encoder_turns(const encoder_t *e);   /* 累積の回転数 (count / 1 回転のカウント数) */
void encoder_reset(encoder_t *e);          /* 累積を 0 に戻す (原点合わせ) */

#ifdef __cplusplus
}
#endif

#endif /* HAL_TIM_MODULE_ENABLED */

#endif
```

`src/mor0koshi_encoder.c`:

```c
/*
 * タイマーのエンコーダーモードで回転を数える。2026_B_446 の main.c (前回との差分) を元にしている。
 * rpm は update を呼んだ間隔を DWT のサイクルカウンタで測って計算するので、呼ぶ周期を指定しなくてよい。
 */
#include "mor0koshi_encoder.h"

#include <string.h>

#include "mor0koshi_port.h"

#if defined(HAL_TIM_MODULE_ENABLED)

lib_status_t encoder_init(encoder_t *e, TIM_HandleTypeDef *htim, int32_t counts_per_rev) {
    if (e == NULL || htim == NULL || counts_per_rev <= 0) {
        return port_fail(LIB_ERR_PARAM, "encoder_init");
    }
    memset(e, 0, sizeof *e);
    e->htim = htim;
    e->cpr = counts_per_rev;
    if (HAL_TIM_Encoder_Start(htim, TIM_CHANNEL_ALL) != HAL_OK) {
        return port_fail(LIB_ERR_HAL, "encoder_init");
    }
    port_cycles_enable();
    e->last_cnt = __HAL_TIM_GET_COUNTER(htim);
    e->last_cycles = port_cycles();
    e->last_ms = HAL_GetTick();
    return LIB_OK;
}

void encoder_set_reverse(encoder_t *e, bool reverse) {
    if (e != NULL) {
        e->reverse = reverse;
    }
}

void encoder_update(encoder_t *e) {
    if (e == NULL || e->htim == NULL) {
        return;
    }
    uint32_t cnt = __HAL_TIM_GET_COUNTER(e->htim);
    uint32_t cycles = port_cycles();
    uint32_t now_ms = HAL_GetTick();

    /*
     * タイマーは ARR の次で 0 に戻る。前回との差を ±半周に折り返して、0 をまたいでも正しく数える。
     * 32bit タイマーでも桁あふれしないよう 64bit で計算する。
     */
    int64_t period = (int64_t)__HAL_TIM_GET_AUTORELOAD(e->htim) + 1;
    int64_t diff = (int64_t)cnt - (int64_t)e->last_cnt;
    if (diff > period / 2) {
        diff -= period;
    } else if (diff < -period / 2) {
        diff += period;
    }
    if (e->reverse) {
        diff = -diff;
    }
    e->delta = (int32_t)diff;
    e->count += e->delta;

    uint32_t dc = cycles - e->last_cycles; /* サイクルカウンタが一周しても、引き算なら正しい間隔になる */
    if (now_ms - e->last_ms >= ENCODER_RPM_RESET_MS) {
        e->rpm = 0.0f; /* 間隔が空きすぎて、サイクルカウンタが何周したか分からない */
    } else if (dc > 0U) {
        /* rpm = (delta / cpr) 回転 ÷ (dc / SystemCoreClock) 秒 × 60 */
        e->rpm = (float)e->delta * 60.0f * (float)SystemCoreClock / ((float)e->cpr * (float)dc);
    }
    e->last_cnt = cnt;
    e->last_cycles = cycles;
    e->last_ms = now_ms;
}

int32_t encoder_count(const encoder_t *e) { return e == NULL ? 0 : e->count; }
int32_t encoder_delta(const encoder_t *e) { return e == NULL ? 0 : e->delta; }
float encoder_rpm(const encoder_t *e) { return e == NULL ? 0.0f : e->rpm; }

float encoder_turns(const encoder_t *e) {
    if (e == NULL || e->cpr <= 0) {
        return 0.0f;
    }
    return (float)e->count / (float)e->cpr;
}

void encoder_reset(encoder_t *e) {
    if (e != NULL) {
        e->count = 0;
    }
}

#endif /* HAL_TIM_MODULE_ENABLED */
```

`CMakeLists.txt` の `add_library` のソースに `${CMAKE_CURRENT_SOURCE_DIR}/src/mor0koshi_encoder.c` を足す (`mor0koshi_sbus.c` の次の行)。

`include/mor0koshi_library.h` に足す:

```c
#include "mor0koshi_encoder.h"
```

- [ ] **Step 5: テストが通ることを確かめる**

Run: (テストの走らせ方)
Expected: `100% tests passed, 0 tests failed out of 4`

- [ ] **Step 6: コミット**

```bash
cd "/home/fukui-ros/STM32 project/mor0koshi_library"
git add CMakeLists.txt include src test
git commit -F - <<'EOF'
エンコーダーの読み取りを追加

16bit・32bit のどちらのタイマーでも、0 をまたいで累積カウントを数える。
rpm は update を呼んだ間隔を DWT で測って計算する。

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task 5: エンコーダー CAN (`mor0koshi_encoder_can`)

**Files:**
- Create: `include/mor0koshi_encoder_can.h`、`src/mor0koshi_encoder_can.c`
- Modify: `CMakeLists.txt`、`include/mor0koshi_library.h`、`test/CMakeLists.txt`
- Test: `test/test_encoder_can.c`

**Interfaces:**
- Consumes: `can_t`、`can_box_t`、`can_send`、`can_listen`、`can_peek`、`can_age_ms`、`can_put_i32`、`can_put_float`、`can_get_i32`、`can_get_float` (Task 2)、`encoder_t`、`encoder_count`、`encoder_rpm` (Task 4)
- Produces:
  - 型 `encoder_can_rx_t`
  - `lib_status_t encoder_can_send(can_t *bus, uint32_t id, const encoder_t *enc);` (TIM が有効なときだけ)
  - `lib_status_t encoder_can_listen(can_t *bus, uint32_t id, encoder_can_rx_t *rx);`
  - `int32_t encoder_can_count(const encoder_can_rx_t *rx);` / `float encoder_can_rpm(const encoder_can_rx_t *rx);` / `uint32_t encoder_can_age_ms(const encoder_can_rx_t *rx);`

- [ ] **Step 1: テストをビルドに足す**

`test/CMakeLists.txt` の `LIB_SOURCES` に `${LIB_DIR}/src/mor0koshi_encoder_can.c` を足し (`mor0koshi_encoder.c` の次の行)、最後に `add_lib_test(test_encoder_can)` を足す。

- [ ] **Step 2: 失敗するテストを書く**

`test/test_encoder_can.c`:

```c
#include "mor0koshi_encoder_can.h"
#include "test.h"

static CAN_HandleTypeDef hcan1 = {.Instance = CAN1};
static can_t can1;
static TIM_TypeDef tim = {.CNT = 0, .ARR = 0xFFFF};
static TIM_HandleTypeDef htim = {.Instance = &tim};
static encoder_t enc;
static encoder_can_rx_t rx;

static void test_send_frame_layout(void) {
    tim.CNT = 0;
    can_init(&can1, &hcan1);
    encoder_init(&enc, &htim, 1000);
    tim.CNT = 65536 - 100; /* -100 カウント */
    fake_dwt.CYCCNT = 1000000; /* 10ms 後 (100MHz) → -0.1 回転 / 10ms = -600rpm */
    fake.tick = 10;
    encoder_update(&enc);
    CHECK_EQ(encoder_can_send(&can1, 0x300, &enc), LIB_OK);
    CHECK_EQ(fake.can_sent_count, 1);
    CHECK_EQ(fake.can_sent[0].header.StdId, 0x300);
    CHECK_EQ(fake.can_sent[0].header.DLC, 8);
    CHECK_EQ(can_get_i32(fake.can_sent[0].data, 0), -100); /* [0..3] 累積カウント */
    CHECK_NEAR(can_get_float(fake.can_sent[0].data, 4), -600.0, 0.01); /* [4..7] rpm */
}

static void test_receive(void) {
    can_init(&can1, &hcan1);
    CHECK_EQ(encoder_can_listen(&can1, 0x301, &rx), LIB_OK);
    CHECK_EQ(encoder_can_count(&rx), 0); /* まだ届いていない */
    CHECK_NEAR(encoder_can_rpm(&rx), 0.0, 1e-9);
    CHECK_EQ(encoder_can_age_ms(&rx), UINT32_MAX);

    uint8_t d[8];
    can_put_i32(d, 0, -123456);
    can_put_float(d, 4, 1500.5f);
    fake.tick = 100;
    fake_can_receive(&hcan1, 0x301, d, 8);
    fake.tick = 130;
    CHECK_EQ(encoder_can_count(&rx), -123456);
    CHECK_NEAR(encoder_can_rpm(&rx), 1500.5, 1e-3);
    CHECK_EQ(encoder_can_age_ms(&rx), 30);
    CHECK_EQ(encoder_can_count(&rx), -123456); /* 何度読んでも同じ値 */
    CHECK_EQ(fake.primask, 0);
}

static void test_null(void) {
    can_init(&can1, &hcan1);
    CHECK_EQ(encoder_can_send(&can1, 0x300, NULL), LIB_ERR_PARAM);
    CHECK_EQ(encoder_can_listen(&can1, 0x300, NULL), LIB_ERR_PARAM);
    CHECK_EQ(encoder_can_count(NULL), 0);
    CHECK_NEAR(encoder_can_rpm(NULL), 0.0, 1e-9);
    CHECK_EQ(encoder_can_age_ms(NULL), UINT32_MAX);
}

int main(void) {
    RUN(test_send_frame_layout);
    RUN(test_receive);
    RUN(test_null);
    return TEST_RESULT();
}
```

- [ ] **Step 3: 失敗することを確かめる**

Run: (テストの走らせ方)
Expected: CMake の設定かビルドが失敗する (`mor0koshi_encoder_can.c` / `mor0koshi_encoder_can.h` が無い)

- [ ] **Step 4: エンコーダー CAN を実装する**

`include/mor0koshi_encoder_can.h`:

```c
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
```

`src/mor0koshi_encoder_can.c`:

```c
/* エンコーダーの値を CAN で送る・受け取る。1 個 = 1 フレーム ([0..3] 累積カウント、[4..7] rpm) */
#include "mor0koshi_encoder_can.h"

#include "mor0koshi_port.h"

#if defined(HAL_CAN_MODULE_ENABLED)

#if defined(HAL_TIM_MODULE_ENABLED)
lib_status_t encoder_can_send(can_t *bus, uint32_t id, const encoder_t *enc) {
    if (enc == NULL) {
        return port_fail(LIB_ERR_PARAM, "encoder_can_send");
    }
    uint8_t d[8];
    can_put_i32(d, 0, encoder_count(enc));
    can_put_float(d, 4, encoder_rpm(enc));
    return can_send(bus, id, d, 8);
}
#endif

lib_status_t encoder_can_listen(can_t *bus, uint32_t id, encoder_can_rx_t *rx) {
    if (rx == NULL) {
        return port_fail(LIB_ERR_PARAM, "encoder_can_listen");
    }
    return can_listen(bus, id, &rx->box);
}

int32_t encoder_can_count(const encoder_can_rx_t *rx) {
    uint8_t d[8];
    if (rx == NULL || !can_peek(&rx->box, d)) {
        return 0;
    }
    return can_get_i32(d, 0);
}

float encoder_can_rpm(const encoder_can_rx_t *rx) {
    uint8_t d[8];
    if (rx == NULL || !can_peek(&rx->box, d)) {
        return 0.0f;
    }
    return can_get_float(d, 4);
}

uint32_t encoder_can_age_ms(const encoder_can_rx_t *rx) {
    return rx == NULL ? UINT32_MAX : can_age_ms(&rx->box);
}

#endif /* HAL_CAN_MODULE_ENABLED */
```

`CMakeLists.txt` の `add_library` のソースに `${CMAKE_CURRENT_SOURCE_DIR}/src/mor0koshi_encoder_can.c` を足す (`mor0koshi_encoder.c` の次の行)。

`include/mor0koshi_library.h` に足す:

```c
#include "mor0koshi_encoder_can.h"
```

- [ ] **Step 5: テストが通ることを確かめる**

Run: (テストの走らせ方)
Expected: `100% tests passed, 0 tests failed out of 5`

- [ ] **Step 6: コミット**

```bash
cd "/home/fukui-ros/STM32 project/mor0koshi_library"
git add CMakeLists.txt include src test
git commit -F - <<'EOF'
エンコーダーの値を CAN で送る・受け取る部品を追加

エンコーダー 1 個を 1 フレーム (累積カウント int32 + rpm float) で送る。
今の 446 → 767 のように 0〜255 に縮めないので、回転方向と精度が残る。

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task 6: ロボマス (`mor0koshi_robomas`)

**Files:**
- Create: `include/mor0koshi_robomas.h`、`src/mor0koshi_robomas.c`
- Modify: `CMakeLists.txt`、`include/mor0koshi_library.h`、`test/CMakeLists.txt`
- Test: `test/test_robomas.c`

**Interfaces:**
- Consumes: `can_t`、`can_send`、`can_subscribe_range` (Task 2)、`port_irq_save` / `port_irq_restore` / `port_fail` (Task 1)
- Produces:
  - 型 `robomas_t`、`robomas_motor_t`、`typedef enum { ROBOMAS_M3508 = 0, ROBOMAS_M2006 = 1 } robomas_type_t;`、定数 `ROBOMAS_MAX_MOTORS` (8)、`ROBOMAS_TIMEOUT_MS` (100)
  - `lib_status_t robomas_init(robomas_t *rm, can_t *bus);`
  - `robomas_motor_t *robomas_add(robomas_t *rm, robomas_type_t type, int id);`
  - `void robomas_set_current(robomas_motor_t *m, int16_t current);` / `void robomas_set_rpm(robomas_motor_t *m, float rpm);` / `void robomas_set_angle(robomas_motor_t *m, float deg);`
  - `lib_status_t robomas_update(robomas_t *rm);` / `void robomas_stop_all(robomas_t *rm);`
  - `float robomas_rpm(const robomas_motor_t *m);` / `float robomas_angle(const robomas_motor_t *m);` / `void robomas_reset_angle(robomas_motor_t *m);` / `int16_t robomas_current(const robomas_motor_t *m);` / `int robomas_temp(const robomas_motor_t *m);` / `bool robomas_ok(const robomas_motor_t *m);`
  - `void robomas_set_gear_ratio(robomas_motor_t *m, float ratio);` / `void robomas_set_speed_gain(robomas_motor_t *m, float kp, float ki, float i_limit);` / `void robomas_set_angle_gain(robomas_motor_t *m, float kp, float max_rpm);`

- [ ] **Step 1: テストをビルドに足す**

`test/CMakeLists.txt` の `LIB_SOURCES` に `${LIB_DIR}/src/mor0koshi_robomas.c` を足し (`mor0koshi_encoder_can.c` の次の行)、最後に `add_lib_test(test_robomas)` を足す。

- [ ] **Step 2: 失敗するテストを書く**

`test/test_robomas.c`:

```c
#include "mor0koshi_robomas.h"
#include "test.h"

static CAN_HandleTypeDef hcan1 = {.Instance = CAN1};
static can_t can1;
static robomas_t rm;

/* ESC からのフィードバックを 1 フレーム受信させる (ビッグエンディアン) */
static void feedback(int esc_id, uint16_t angle, int16_t rpm, int16_t current, uint8_t temp) {
    uint8_t d[8] = {
        (uint8_t)(angle >> 8), (uint8_t)angle,
        (uint8_t)((uint16_t)rpm >> 8), (uint8_t)rpm,
        (uint8_t)((uint16_t)current >> 8), (uint8_t)current,
        temp, 0,
    };
    fake_can_receive(&hcan1, 0x200 + (uint32_t)esc_id, d, 8);
}

/* frame 番目に送った電流指令フレームから、ESC の ID の値を取り出す (ビッグエンディアン) */
static int16_t sent_current(int frame, int esc_id) {
    const uint8_t *d = fake.can_sent[frame].data;
    int slot = (esc_id - 1) % 4;
    return (int16_t)((d[slot * 2] << 8) | d[slot * 2 + 1]);
}

static void setup(void) {
    can_init(&can1, &hcan1);
    CHECK_EQ(robomas_init(&rm, &can1), LIB_OK);
}

static void test_add(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M3508, 1);
    CHECK(m != NULL);
    CHECK(robomas_add(&rm, ROBOMAS_M3508, 1) == NULL); /* 同じ ID は二重に登録できない */
    CHECK_EQ(fake.last_error, LIB_ERR_PARAM);
    CHECK(robomas_add(&rm, ROBOMAS_M2006, 0) == NULL);
    CHECK(robomas_add(&rm, ROBOMAS_M2006, 9) == NULL);
    CHECK(robomas_add(&rm, ROBOMAS_M2006, 8) != NULL);
}

static void test_update_without_motors_sends_nothing(void) {
    setup();
    CHECK_EQ(robomas_update(&rm), LIB_OK);
    CHECK_EQ(fake.can_sent_count, 0);
}

static void test_feedback_parse(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M3508, 2);
    CHECK(!robomas_ok(m));
    fake.tick = 100;
    feedback(2, 1000, -1920, -300, 45);
    CHECK(robomas_ok(m));
    CHECK_NEAR(robomas_rpm(m), -1920.0 / (3591.0 / 187.0), 1e-3); /* 出力軸 rpm */
    CHECK_EQ(robomas_current(m), -300);
    CHECK_EQ(robomas_temp(m), 45);
    CHECK_NEAR(robomas_angle(m), 0.0, 1e-9); /* 最初のフィードバックの位置が 0 度 */
    fake.tick = 199;
    CHECK(robomas_ok(m));
    fake.tick = 200;
    CHECK(!robomas_ok(m)); /* 100ms 来なければ NG */
}

static void test_feedback_for_unregistered_id_is_ignored(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M3508, 1);
    feedback(3, 1000, 500, 0, 30);
    CHECK(!robomas_ok(m));
}

static void test_m2006_temp_is_zero(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    feedback(1, 0, 0, 0, 99); /* C610 は温度を返さない */
    CHECK_EQ(robomas_temp(m), 0);
}

static void test_multi_turn_angle(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f); /* ローターの角度で確かめる */
    feedback(1, 8000, 0, 0, 0);      /* ここが 0 度 */
    feedback(1, 100, 0, 0, 0);       /* 8191 → 0 をまたいで +292 */
    CHECK_NEAR(robomas_angle(m), 292.0 * 360.0 / 8192.0, 1e-3);
    for (int i = 0; i < 10; i++) { /* 2048 ずつ 4 回 = 1 回転、を 10 回転分 */
        for (int k = 1; k <= 4; k++) {
            feedback(1, (uint16_t)((100 + 2048 * k) % 8192), 0, 0, 0);
        }
    }
    CHECK_NEAR(robomas_angle(m), 3600.0 + 292.0 * 360.0 / 8192.0, 1e-2);
    feedback(1, 8000, 0, 0, 0); /* 0 → 8191 を逆向きにまたいで -292 */
    CHECK_NEAR(robomas_angle(m), 3600.0, 1e-2);
}

static void test_angle_uses_gear_ratio_and_reset(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1); /* ギア比 36 */
    feedback(1, 0, 0, 0, 0);
    for (int k = 1; k <= 36 * 4; k++) { /* ローター 36 回転 */
        feedback(1, (uint16_t)((2048 * k) % 8192), 0, 0, 0);
    }
    CHECK_NEAR(robomas_angle(m), 360.0, 1e-2); /* 出力軸は 1 回転 */
    robomas_reset_angle(m);
    CHECK_NEAR(robomas_angle(m), 0.0, 1e-9);
    feedback(1, 2048, 0, 0, 0); /* ローター 1/4 回転 = 出力軸 2.5 度 */
    CHECK_NEAR(robomas_angle(m), 90.0 / 36.0, 1e-3);
    CHECK_EQ(fake.primask, 0);
}

static void test_current_mode_frames(void) {
    setup();
    robomas_motor_t *m1 = robomas_add(&rm, ROBOMAS_M3508, 1);
    robomas_motor_t *m3 = robomas_add(&rm, ROBOMAS_M3508, 3);
    feedback(1, 0, 0, 0, 0);
    feedback(3, 0, 0, 0, 0);
    robomas_set_current(m1, 1000);
    robomas_set_current(m3, -2000);
    CHECK_EQ(robomas_update(&rm), LIB_OK);
    CHECK_EQ(fake.can_sent_count, 1); /* ID 5〜8 がいないので 0x1FF は送らない */
    CHECK_EQ(fake.can_sent[0].header.StdId, 0x200);
    CHECK_EQ(fake.can_sent[0].header.DLC, 8);
    CHECK_EQ(sent_current(0, 1), 1000);
    CHECK_EQ(sent_current(0, 2), 0); /* 登録していない ID は 0 */
    CHECK_EQ(sent_current(0, 3), -2000);
    CHECK_EQ(fake.can_sent[0].data[0], 0x03); /* 1000 = 0x03E8。ビッグエンディアン */
    CHECK_EQ(fake.can_sent[0].data[1], 0xE8);
}

static void test_high_ids_use_0x1ff(void) {
    setup();
    robomas_motor_t *m6 = robomas_add(&rm, ROBOMAS_M2006, 6);
    feedback(6, 0, 0, 0, 0);
    robomas_set_current(m6, 500);
    robomas_update(&rm);
    CHECK_EQ(fake.can_sent_count, 1); /* ID 1〜4 がいないので 0x200 は送らない */
    CHECK_EQ(fake.can_sent[0].header.StdId, 0x1FF);
    CHECK_EQ(sent_current(0, 6), 500);
}

static void test_both_frames(void) {
    setup();
    robomas_motor_t *a = robomas_add(&rm, ROBOMAS_M3508, 1);
    robomas_motor_t *b = robomas_add(&rm, ROBOMAS_M2006, 5);
    feedback(1, 0, 0, 0, 0);
    feedback(5, 0, 0, 0, 0);
    robomas_set_current(a, 111);
    robomas_set_current(b, 222);
    robomas_update(&rm);
    CHECK_EQ(fake.can_sent_count, 2);
    CHECK_EQ(fake.can_sent[0].header.StdId, 0x200);
    CHECK_EQ(fake.can_sent[1].header.StdId, 0x1FF);
    CHECK_EQ(sent_current(0, 1), 111);
    CHECK_EQ(sent_current(1, 5), 222);
}

static void test_current_is_clamped(void) {
    setup();
    robomas_motor_t *a = robomas_add(&rm, ROBOMAS_M3508, 1);
    robomas_motor_t *b = robomas_add(&rm, ROBOMAS_M2006, 2);
    feedback(1, 0, 0, 0, 0);
    feedback(2, 0, 0, 0, 0);
    robomas_set_current(a, 30000);
    robomas_set_current(b, -30000);
    robomas_update(&rm);
    CHECK_EQ(sent_current(0, 1), 16384);  /* M3508 は ±16384 */
    CHECK_EQ(sent_current(0, 2), -10000); /* M2006 は ±10000 */
}

static void test_no_feedback_yet_outputs_zero(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M3508, 1);
    robomas_set_current(m, 1000);
    robomas_update(&rm);
    CHECK_EQ(sent_current(0, 1), 0);
}

static void test_speed_pi(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1); /* kp 1.5 / ki 0.08 / ギア比 36 */
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 10.0f); /* 出力軸 10rpm = ローター 360rpm */
    robomas_update(&rm);
    CHECK_EQ(sent_current(0, 1), 568); /* 誤差 360、積分 360 → 1.5 * 360 + 0.08 * 360 = 568.8 */
    feedback(1, 0, 300, 0, 0);         /* ローター 300rpm まで上がった */
    robomas_update(&rm);
    CHECK_EQ(sent_current(1, 1), 123); /* 誤差 60、積分 420 → 1.5 * 60 + 0.08 * 420 = 123.6 */
}

static void test_integral_is_limited(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    feedback(1, 0, 0, 0, 0);
    robomas_set_speed_gain(m, 0.0f, 1.0f, 5000.0f); /* 積分だけ */
    robomas_set_rpm(m, 100.0f);                     /* ローター 3600rpm。誤差 3600 */
    robomas_update(&rm);
    CHECK_EQ(sent_current(0, 1), 3600);
    robomas_update(&rm);
    CHECK_EQ(sent_current(1, 1), 5000); /* 積分 7200 → 5000 で頭打ち */
}

static void test_angle_mode(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 1.0f);
    robomas_set_speed_gain(m, 1.0f, 0.0f, 5000.0f); /* P だけ */
    robomas_set_angle_gain(m, 2.0f, 100.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_angle(m, 10.0f); /* 誤差 10 度 → 目標 20rpm → 電流 20 */
    robomas_update(&rm);
    CHECK_EQ(sent_current(0, 1), 20);
    robomas_set_angle(m, 360.0f); /* 誤差 360 度 → 720rpm だが 100rpm で頭打ち */
    robomas_update(&rm);
    CHECK_EQ(sent_current(1, 1), 100);
    robomas_set_angle(m, -360.0f);
    robomas_update(&rm);
    CHECK_EQ(sent_current(2, 1), -100);
}

static void test_mode_change_resets_integral(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_speed_gain(m, 0.0f, 1.0f, 5000.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 10.0f); /* 誤差 360 */
    robomas_update(&rm);       /* 積分 360 */
    robomas_set_current(m, 0);
    robomas_set_rpm(m, 10.0f);
    robomas_update(&rm);
    CHECK_EQ(sent_current(1, 1), 360); /* 積分が 0 からやり直しなので 720 にはならない */
}

static void test_lost_feedback_outputs_zero_and_resets_integral(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_speed_gain(m, 0.0f, 1.0f, 5000.0f);
    feedback(1, 0, 0, 0, 0);
    robomas_set_rpm(m, 10.0f);
    robomas_update(&rm); /* 積分 360 */
    fake.tick = 100;     /* フィードバックが 100ms 途絶えた */
    robomas_update(&rm);
    CHECK_EQ(sent_current(1, 1), 0);
    feedback(1, 0, 0, 0, 0); /* 復帰 */
    robomas_update(&rm);
    CHECK_EQ(sent_current(2, 1), 360); /* 積分はリセットされている (720 にならない) */
}

static void test_stop_all(void) {
    setup();
    robomas_motor_t *a = robomas_add(&rm, ROBOMAS_M3508, 1);
    robomas_motor_t *b = robomas_add(&rm, ROBOMAS_M2006, 5);
    feedback(1, 0, 0, 0, 0);
    feedback(5, 0, 0, 0, 0);
    robomas_set_rpm(a, 50.0f);
    robomas_set_current(b, 3000);
    robomas_stop_all(&rm);
    robomas_update(&rm);
    CHECK_EQ(sent_current(0, 1), 0);
    CHECK_EQ(sent_current(1, 5), 0);
}

static void test_bad_settings_and_null(void) {
    setup();
    robomas_motor_t *m = robomas_add(&rm, ROBOMAS_M2006, 1);
    robomas_set_gear_ratio(m, 0.0f);
    CHECK_EQ(fake.last_error, LIB_ERR_PARAM);
    feedback(1, 0, 360, 0, 0);
    CHECK_NEAR(robomas_rpm(m), 10.0, 1e-6); /* ギア比は 36 のまま */
    CHECK(robomas_add(NULL, ROBOMAS_M3508, 1) == NULL);
    robomas_set_rpm(NULL, 1.0f);
    robomas_stop_all(NULL);
    CHECK_NEAR(robomas_angle(NULL), 0.0, 1e-9);
    CHECK(!robomas_ok(NULL));
    CHECK_EQ(robomas_update(NULL), LIB_ERR_PARAM);
    CHECK_EQ(robomas_init(NULL, &can1), LIB_ERR_PARAM);
}

int main(void) {
    RUN(test_add);
    RUN(test_update_without_motors_sends_nothing);
    RUN(test_feedback_parse);
    RUN(test_feedback_for_unregistered_id_is_ignored);
    RUN(test_m2006_temp_is_zero);
    RUN(test_multi_turn_angle);
    RUN(test_angle_uses_gear_ratio_and_reset);
    RUN(test_current_mode_frames);
    RUN(test_high_ids_use_0x1ff);
    RUN(test_both_frames);
    RUN(test_current_is_clamped);
    RUN(test_no_feedback_yet_outputs_zero);
    RUN(test_speed_pi);
    RUN(test_integral_is_limited);
    RUN(test_angle_mode);
    RUN(test_mode_change_resets_integral);
    RUN(test_lost_feedback_outputs_zero_and_resets_integral);
    RUN(test_stop_all);
    RUN(test_bad_settings_and_null);
    return TEST_RESULT();
}
```

- [ ] **Step 3: 失敗することを確かめる**

Run: (テストの走らせ方)
Expected: CMake の設定かビルドが失敗する (`mor0koshi_robomas.c` / `mor0koshi_robomas.h` が無い)

- [ ] **Step 4: ロボマスを実装する**

`include/mor0koshi_robomas.h`:

```c
#ifndef MOR0KOSHI_ROBOMAS_H
#define MOR0KOSHI_ROBOMAS_H
/*
 * ロボマスモーター M3508 (C620) / M2006 (C610) の速度制御・位置制御。
 *
 *   robomas_t rm;                                              // 1 本の CAN につき 1 個
 *   robomas_init(&rm, &can1);
 *   robomas_motor_t *wheel = robomas_add(&rm, ROBOMAS_M3508, 1);  // 種類と ESC の ID (1〜8)
 *   robomas_set_rpm(wheel, 300.0f);                            // 出力軸 rpm
 *   // 1ms ごとに 1 回
 *   robomas_update(&rm);
 *
 * 使う側に見える値は全部「出力軸」(ギアの先)。ギア比は種類から自動で入る。
 * ゲインは 1ms ごとに robomas_update を呼ぶ前提。M3508 と位置制御の初期ゲインは仮の値なので実機で調整すること。
 * ロボマスは 1 台で 1Mbps の CAN の約 13% を使う。1 本の CAN には 5 台くらいまでにする。
 */
#include <stdbool.h>
#include <stdint.h>

#include "mor0koshi_can.h"

#if defined(HAL_CAN_MODULE_ENABLED)

#ifdef __cplusplus
extern "C" {
#endif

#define ROBOMAS_MAX_MOTORS 8
#define ROBOMAS_TIMEOUT_MS 100 /* フィードバックがこれ以上来なければ robomas_ok が false */

typedef enum {
    ROBOMAS_M3508 = 0, /* C620。ギア比 3591/187、電流 ±16384 */
    ROBOMAS_M2006 = 1, /* C610。ギア比 36、電流 ±10000 */
} robomas_type_t;

/* ここから下の 2 つは robomas_t の中身。直接さわらない */
typedef enum {
    ROBOMAS_MODE_CURRENT = 0,
    ROBOMAS_MODE_SPEED,
    ROBOMAS_MODE_ANGLE,
} robomas_mode_t;

typedef struct robomas_motor {
    bool used;
    robomas_type_t type;
    uint8_t id;    /* ESC の ID (1〜8) */
    float gear;    /* ギア比 (ローターの回転数 / 出力軸の回転数) */
    int16_t current_limit;
    float speed_kp;
    float speed_ki;
    float speed_i_limit;
    float angle_kp;
    float angle_max_rpm;
    robomas_mode_t mode;
    float target;   /* 電流: 生の値 / 速度: 出力軸 rpm / 位置: 出力軸の角度 [度] */
    float integral; /* 速度 PI の積分 (ローター rpm の誤差の和) */
    int16_t output; /* 最後に送った電流指令 */
    /* 受信割り込みが書く */
    volatile bool fb_started;
    volatile uint16_t fb_raw_angle;
    volatile int64_t fb_total; /* ローターの累積角度 (8192 で 1 回転) */
    volatile int16_t fb_rpm;   /* ローター rpm */
    volatile int16_t fb_current;
    volatile uint8_t fb_temp;
    volatile uint32_t fb_ms;
} robomas_motor_t;

typedef struct robomas {
    can_t *bus;
    robomas_motor_t motors[ROBOMAS_MAX_MOTORS]; /* motors[i] が ESC の ID i+1 */
} robomas_t;

lib_status_t robomas_init(robomas_t *rm, can_t *bus);
/* モーターを登録する。ID が範囲外 (1〜8 以外) か、登録済みなら NULL */
robomas_motor_t *robomas_add(robomas_t *rm, robomas_type_t type, int id);

/* 指令 (最後に呼んだものが有効。種類を変えると積分をリセットする) */
void robomas_set_current(robomas_motor_t *m, int16_t current); /* 電流を直接 (種類ごとの範囲で頭打ち) */
void robomas_set_rpm(robomas_motor_t *m, float rpm);           /* 速度制御: 出力軸 rpm */
void robomas_set_angle(robomas_motor_t *m, float deg);         /* 位置制御: 出力軸の角度 [度] */

/* 全モーターの制御を計算して、使っている ID の範囲の 0x200 / 0x1FF を送る。1ms ごとに呼ぶ */
lib_status_t robomas_update(robomas_t *rm);
/* 全モーターを電流指令 0 にして積分をリセットする (送るのは次の robomas_update)。SBUS が NG のときに呼ぶ */
void robomas_stop_all(robomas_t *rm);

float robomas_rpm(const robomas_motor_t *m);       /* 出力軸 rpm */
float robomas_angle(const robomas_motor_t *m);     /* 出力軸の累積角度 [度] (最初のフィードバックの位置が 0 度) */
void robomas_reset_angle(robomas_motor_t *m);      /* 今の位置を 0 度にする */
int16_t robomas_current(const robomas_motor_t *m); /* 実電流 (生の値) */
int robomas_temp(const robomas_motor_t *m);        /* 温度 ℃ (M3508 のみ。M2006 は 0) */
bool robomas_ok(const robomas_motor_t *m);         /* フィードバックが 100ms 以内に届いているか */

void robomas_set_gear_ratio(robomas_motor_t *m, float ratio);
void robomas_set_speed_gain(robomas_motor_t *m, float kp, float ki, float i_limit);
void robomas_set_angle_gain(robomas_motor_t *m, float kp, float max_rpm);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CAN_MODULE_ENABLED */

#endif
```

`src/mor0koshi_robomas.c`:

```c
/*
 * ロボマスモーター M3508 (C620) / M2006 (C610)。AIcontest の main.c を元にしている。
 *   指令: 0x200 (ESC の ID 1〜4)、0x1FF (ID 5〜8)。各 2 バイトの int16、ビッグエンディアン
 *   フィードバック: 0x200 + ID。[0..1] 角度 0〜8191、[2..3] ローター rpm、[4..5] 電流、[6] 温度 (C620 のみ)
 */
#include "mor0koshi_robomas.h"

#include <string.h>

#include "mor0koshi_port.h"

#if defined(HAL_CAN_MODULE_ENABLED)

#define ROBOMAS_FB_FIRST 0x201U
#define ROBOMAS_FB_LAST 0x208U
#define ROBOMAS_CMD_LOW 0x200U  /* ESC の ID 1〜4 への電流指令 */
#define ROBOMAS_CMD_HIGH 0x1FFU /* ESC の ID 5〜8 への電流指令 */
#define ROBOMAS_COUNTS_PER_REV 8192

static float clampf(float v, float lim) {
    if (v > lim) {
        return lim;
    }
    if (v < -lim) {
        return -lim;
    }
    return v;
}

/*
 * フィードバック (受信割り込みの中)。
 * 生の角度は 1 回転 (0〜8191) で一周するので、届くたびに前回との差を -4096〜+4095 に折り返して足す。
 * 1ms ごとに届き、最高回転 (約 9000rpm) でも 1ms で半回転未満なので数え間違えない。
 * robomas_update の周期とは関係なく正しく数えるために、ここ (割り込みの中) でやる。
 */
static void on_feedback(uint32_t id, const uint8_t *d, uint8_t len, void *arg) {
    robomas_t *rm = arg;
    robomas_motor_t *m = &rm->motors[id - ROBOMAS_FB_FIRST];
    if (!m->used || len < 7U) {
        return;
    }
    uint16_t raw = (uint16_t)((d[0] << 8) | d[1]);
    if (m->fb_started) {
        int32_t diff = (int32_t)raw - (int32_t)m->fb_raw_angle;
        if (diff > 4095) {
            diff -= ROBOMAS_COUNTS_PER_REV;
        } else if (diff < -4096) {
            diff += ROBOMAS_COUNTS_PER_REV;
        }
        m->fb_total += diff;
    }
    m->fb_raw_angle = raw;
    m->fb_rpm = (int16_t)((d[2] << 8) | d[3]);
    m->fb_current = (int16_t)((d[4] << 8) | d[5]);
    m->fb_temp = d[6];
    m->fb_ms = HAL_GetTick(); /* fb_started より先に書く */
    m->fb_started = true;
}

lib_status_t robomas_init(robomas_t *rm, can_t *bus) {
    if (rm == NULL || bus == NULL) {
        return port_fail(LIB_ERR_PARAM, "robomas_init");
    }
    memset(rm, 0, sizeof *rm);
    rm->bus = bus;
    return can_subscribe_range(bus, ROBOMAS_FB_FIRST, ROBOMAS_FB_LAST, on_feedback, rm);
}

robomas_motor_t *robomas_add(robomas_t *rm, robomas_type_t type, int id) {
    if (rm == NULL || id < 1 || id > ROBOMAS_MAX_MOTORS || (type != ROBOMAS_M3508 && type != ROBOMAS_M2006) ||
        rm->motors[id - 1].used) {
        lib_error_hook(LIB_ERR_PARAM, "robomas_add");
        return NULL;
    }
    robomas_motor_t *m = &rm->motors[id - 1];
    uint32_t pm = port_irq_save();
    memset(m, 0, sizeof *m);
    m->type = type;
    m->id = (uint8_t)id;
    if (type == ROBOMAS_M3508) {
        m->gear = 3591.0f / 187.0f;
        m->current_limit = 16384;
    } else {
        m->gear = 36.0f;
        m->current_limit = 10000;
    }
    /* M2006 の速度ゲインは AIcontest で確認済みの値。M3508 と位置制御は仮の値 (実機で調整する) */
    m->speed_kp = 1.5f;
    m->speed_ki = 0.08f;
    m->speed_i_limit = 5000.0f;
    m->angle_kp = 2.0f;
    m->angle_max_rpm = 100.0f;
    m->mode = ROBOMAS_MODE_CURRENT;
    m->target = 0.0f;
    m->used = true; /* 設定を書き終えてから使う印を立てる (割り込みが途中の状態を見ないように) */
    port_irq_restore(pm);
    return m;
}

/* 制御の種類が変わったら積分を捨てる (前の種類の積分が残ると、切り替えた瞬間に飛び出す) */
static void set_mode(robomas_motor_t *m, robomas_mode_t mode) {
    if (m->mode != mode) {
        m->mode = mode;
        m->integral = 0.0f;
    }
}

void robomas_set_current(robomas_motor_t *m, int16_t current) {
    if (m == NULL) {
        return;
    }
    set_mode(m, ROBOMAS_MODE_CURRENT);
    m->target = (float)current;
}

void robomas_set_rpm(robomas_motor_t *m, float rpm) {
    if (m == NULL) {
        return;
    }
    set_mode(m, ROBOMAS_MODE_SPEED);
    m->target = rpm;
}

void robomas_set_angle(robomas_motor_t *m, float deg) {
    if (m == NULL) {
        return;
    }
    set_mode(m, ROBOMAS_MODE_ANGLE);
    m->target = deg;
}

bool robomas_ok(const robomas_motor_t *m) {
    if (m == NULL || !m->fb_started) {
        return false;
    }
    uint32_t fb_ms = m->fb_ms; /* 先に読む (後から HAL_GetTick を読めば、差が負にならない) */
    return HAL_GetTick() - fb_ms < ROBOMAS_TIMEOUT_MS;
}

float robomas_rpm(const robomas_motor_t *m) {
    if (m == NULL) {
        return 0.0f;
    }
    return (float)m->fb_rpm / m->gear;
}

/* 64bit の値は 1 命令で読めないので、割り込みを止めて読む */
static int64_t read_total(const robomas_motor_t *m) {
    uint32_t pm = port_irq_save();
    int64_t total = m->fb_total;
    port_irq_restore(pm);
    return total;
}

float robomas_angle(const robomas_motor_t *m) {
    if (m == NULL) {
        return 0.0f;
    }
    return (float)read_total(m) * (360.0f / (float)ROBOMAS_COUNTS_PER_REV) / m->gear;
}

void robomas_reset_angle(robomas_motor_t *m) {
    if (m == NULL) {
        return;
    }
    uint32_t pm = port_irq_save();
    m->fb_total = 0;
    port_irq_restore(pm);
}

int16_t robomas_current(const robomas_motor_t *m) { return m == NULL ? 0 : m->fb_current; }

int robomas_temp(const robomas_motor_t *m) {
    if (m == NULL || m->type == ROBOMAS_M2006) {
        return 0; /* C610 は温度を返さない */
    }
    return m->fb_temp;
}

void robomas_set_gear_ratio(robomas_motor_t *m, float ratio) {
    if (m == NULL || !(ratio > 0.0f)) {
        lib_error_hook(LIB_ERR_PARAM, "robomas_set_gear_ratio");
        return;
    }
    m->gear = ratio;
}

void robomas_set_speed_gain(robomas_motor_t *m, float kp, float ki, float i_limit) {
    if (m == NULL || i_limit < 0.0f) {
        lib_error_hook(LIB_ERR_PARAM, "robomas_set_speed_gain");
        return;
    }
    m->speed_kp = kp;
    m->speed_ki = ki;
    m->speed_i_limit = i_limit;
    m->integral = clampf(m->integral, i_limit);
}

void robomas_set_angle_gain(robomas_motor_t *m, float kp, float max_rpm) {
    if (m == NULL || max_rpm < 0.0f) {
        lib_error_hook(LIB_ERR_PARAM, "robomas_set_angle_gain");
        return;
    }
    m->angle_kp = kp;
    m->angle_max_rpm = max_rpm;
}

/* 速度 PI。目標は出力軸 rpm。ギア比を掛けてローター rpm で計算するので、ギア比を変えてもゲインは同じ */
static float speed_pi(robomas_motor_t *m, float out_rpm) {
    float err = out_rpm * m->gear - (float)m->fb_rpm;
    m->integral = clampf(m->integral + err, m->speed_i_limit);
    return m->speed_kp * err + m->speed_ki * m->integral;
}

static int16_t compute(robomas_motor_t *m) {
    if (!robomas_ok(m)) {
        /* フィードバックが無いのに積分がたまり続けると、復帰した瞬間に暴れる */
        m->integral = 0.0f;
        return 0;
    }
    float cmd;
    switch (m->mode) {
    case ROBOMAS_MODE_SPEED:
        cmd = speed_pi(m, m->target);
        break;
    case ROBOMAS_MODE_ANGLE:
        /* 角度の差に比例した目標 rpm を作り (上限で頭打ち)、速度制御に渡す */
        cmd = speed_pi(m, clampf(m->angle_kp * (m->target - robomas_angle(m)), m->angle_max_rpm));
        break;
    case ROBOMAS_MODE_CURRENT:
    default:
        cmd = m->target;
        break;
    }
    return (int16_t)clampf(cmd, (float)m->current_limit);
}

static void put_be16(uint8_t *d, int offset, int16_t v) {
    d[offset] = (uint8_t)((uint16_t)v >> 8);
    d[offset + 1] = (uint8_t)v;
}

lib_status_t robomas_update(robomas_t *rm) {
    if (rm == NULL || rm->bus == NULL) {
        return port_fail(LIB_ERR_PARAM, "robomas_update");
    }
    uint8_t low[8] = {0};
    uint8_t high[8] = {0};
    bool use_low = false;
    bool use_high = false;
    for (int i = 0; i < ROBOMAS_MAX_MOTORS; i++) {
        robomas_motor_t *m = &rm->motors[i];
        if (!m->used) {
            continue;
        }
        m->output = compute(m);
        if (i < 4) {
            put_be16(low, i * 2, m->output);
            use_low = true;
        } else {
            put_be16(high, (i - 4) * 2, m->output);
            use_high = true;
        }
    }
    lib_status_t st = LIB_OK;
    if (use_low) {
        st = can_send(rm->bus, ROBOMAS_CMD_LOW, low, 8);
    }
    if (use_high) {
        lib_status_t st_high = can_send(rm->bus, ROBOMAS_CMD_HIGH, high, 8);
        if (st == LIB_OK) {
            st = st_high;
        }
    }
    return st;
}

void robomas_stop_all(robomas_t *rm) {
    if (rm == NULL) {
        return;
    }
    for (int i = 0; i < ROBOMAS_MAX_MOTORS; i++) {
        robomas_motor_t *m = &rm->motors[i];
        if (m->used) {
            m->mode = ROBOMAS_MODE_CURRENT;
            m->target = 0.0f;
            m->integral = 0.0f;
        }
    }
}

#endif /* HAL_CAN_MODULE_ENABLED */
```

`CMakeLists.txt` の `add_library` のソースに `${CMAKE_CURRENT_SOURCE_DIR}/src/mor0koshi_robomas.c` を足す (`mor0koshi_encoder_can.c` の次の行)。

`include/mor0koshi_library.h` に足す:

```c
#include "mor0koshi_robomas.h"
```

- [ ] **Step 5: テストが通ることを確かめる**

Run: (テストの走らせ方)
Expected: `100% tests passed, 0 tests failed out of 6`

- [ ] **Step 6: コミット**

```bash
cd "/home/fukui-ros/STM32 project/mor0koshi_library"
git add CMakeLists.txt include src test
git commit -F - <<'EOF'
ロボマスモーター (M3508 / M2006) の制御を追加

電流・速度 (PI)・位置 (P → 速度 PI) の 3 つの指令に対応した。値は全部出力軸で扱い、
角度は受信割り込みの中で積算するので何回転しても数えられる。
フィードバックが 100ms 途絶えたモーターは出力 0 にして積分を捨てる。

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task 7: 例と ARM でのビルド確認

**Files:**
- Create: `examples/robomas_sbus.c`、`examples/encoder_sender.c`、`examples/encoder_receiver.c`
- Create: `test/arm_build_check.sh`、`test/linkcheck_f446.c`、`test/linkcheck_nocan.c`
- Modify: `CMakeLists.txt` (例のビルド確認用のオプション)

**Interfaces:**
- Consumes: Task 1〜6 の公開関数すべて
- Produces: `MOR0KOSHI_BUILD_EXAMPLES` オプション (CMake)、`example_robomas_sbus_init/loop`、`example_encoder_sender_init/loop`、`example_encoder_receiver_init/loop`

このタスクは PC のテストではなく、本物の CubeMX プロジェクトの設定 (HAL・コンパイルオプション) でビルドが通ることを確かめる。元のプロジェクトには触らず、`arm_build_check.sh` が一時フォルダにコピーしてビルドする。

このセッション (Claude Code) で実行するときは、一時フォルダをスクラッチ領域に作るよう `TMPDIR=<スクラッチ領域のパス>` を付けて走らせる。

2026_B_767 と AIcontest は HAL の CAN・UART コールバックを自分で書いているので、ライブラリをリンクすると衝突する (multiple definition)。リンクまでの確認には、コールバックを書いていない 2026_B_446 (F446) と mortor_test (F767、CAN なし) を使い、2026_B_767 はコンパイルだけ確かめる。

- [ ] **Step 1: 例を書く**

`examples/robomas_sbus.c`:

```c
/*
 * 例: プロポ (SBUS) でロボマスモーターを回す
 *
 * CubeMX の設定:
 *   - CAN1: 1Mbps、NVIC で CAN1 RX0 と CAN1 TX の割り込みをオン
 *   - UART5: 100000bps、9bit (偶数パリティ)、ストップビット 2、RX 反転 (F7)、RX の DMA (Normal)、UART5 の割り込みをオン
 *   - ESC: M3508 を ID 1、M2006 を ID 5 に設定しておく
 *
 * main.c の USER CODE BEGIN 2 で example_robomas_sbus_init() を 1 回、
 * while (1) の中で example_robomas_sbus_loop() を呼ぶ。
 */
#include "mor0koshi_library.h"

#if defined(HAL_CAN_MODULE_ENABLED) && defined(HAL_UART_MODULE_ENABLED)

extern CAN_HandleTypeDef hcan1;
extern UART_HandleTypeDef huart5;

static can_t can1;
static sbus_t sbus;
static robomas_t rm;
static robomas_motor_t *wheel;
static robomas_motor_t *arm;
static uint32_t last_ms;

void example_robomas_sbus_init(void) {
    can_init(&can1, &hcan1);
    sbus_init(&sbus, &huart5);
    robomas_init(&rm, &can1);
    wheel = robomas_add(&rm, ROBOMAS_M3508, 1);
    arm = robomas_add(&rm, ROBOMAS_M2006, 5);
}

void example_robomas_sbus_loop(void) {
    uint32_t now = HAL_GetTick();
    if (now == last_ms) {
        return; /* 1ms に 1 回だけ */
    }
    last_ms = now;

    sbus_update(&sbus);
    if (!sbus_ok(&sbus)) {
        robomas_stop_all(&rm); /* プロポが切れたら全部止める */
    } else {
        /* 左スティック上下 (ch1) で M3508 を ±300rpm */
        robomas_set_rpm(wheel, (float)sbus_stick(&sbus, 1) * 0.3f);
        /* 3 段スイッチ (ch6) で M2006 を -90 / 0 / +90 度 */
        robomas_set_angle(arm, (float)sbus_switch3(&sbus, 6) * 90.0f);
    }
    robomas_update(&rm); /* 止めているときも呼び続ける (電流 0 を送り続ける) */
}

#endif
```

`examples/encoder_sender.c`:

```c
/*
 * 例: エンコーダー 2 個の値を CAN で別の基板に送る (2026_B_446 のような基板)
 *
 * CubeMX の設定:
 *   - CAN1: 1Mbps、NVIC で CAN1 RX0 と CAN1 TX の割り込みをオン
 *   - TIM1・TIM2: Encoder Mode (TI1 and TI2)、Prescaler 0、Period 最大
 *
 * main.c の USER CODE BEGIN 2 で example_encoder_sender_init() を 1 回、
 * while (1) の中で example_encoder_sender_loop() を呼ぶ。
 */
#include "mor0koshi_library.h"

#if defined(HAL_CAN_MODULE_ENABLED) && defined(HAL_TIM_MODULE_ENABLED)

extern CAN_HandleTypeDef hcan1;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;

static can_t can1;
static encoder_t enc1;
static encoder_t enc2;
static uint32_t last_ms;

void example_encoder_sender_init(void) {
    can_init(&can1, &hcan1);
    encoder_init(&enc1, &htim1, 2048); /* 512 パルスのエンコーダー × 4 逓倍 */
    encoder_init(&enc2, &htim2, 2048);
}

void example_encoder_sender_loop(void) {
    uint32_t now = HAL_GetTick();
    if (now - last_ms < 10) {
        return; /* 10ms ごと (2 個で CAN の約 3%) */
    }
    last_ms = now;
    encoder_update(&enc1);
    encoder_update(&enc2);
    encoder_can_send(&can1, 0x300, &enc1);
    encoder_can_send(&can1, 0x301, &enc2);
}

#endif
```

`examples/encoder_receiver.c`:

```c
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
```

`CMakeLists.txt` の最後に足す:

```cmake
# examples/ をコンパイルだけする (ビルド確認用。使う側のプロジェクトにはリンクしない)
option(MOR0KOSHI_BUILD_EXAMPLES "examples/ もコンパイルする (ビルド確認用)" OFF)
if(MOR0KOSHI_BUILD_EXAMPLES)
    add_library(mor0koshi_examples OBJECT
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/robomas_sbus.c
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/encoder_sender.c
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/encoder_receiver.c
    )
    target_link_libraries(mor0koshi_examples PRIVATE mor0koshi_library)
    target_compile_options(mor0koshi_examples PRIVATE -Wall -Wextra -Wdouble-promotion)
endif()
```

- [ ] **Step 2: ビルド確認のスクリプトとリンク確認用のファイルを書く**

`test/arm_build_check.sh`:

```bash
#!/usr/bin/env bash
# 本物の CubeMX プロジェクトの設定 (HAL・コンパイルオプション) でライブラリがビルドできるか確かめる。
# 元のプロジェクトには触らず、一時フォルダにコピーしてビルドする。
#
# 使い方:
#   test/arm_build_check.sh <CubeMX プロジェクトのフォルダ> compile
#       ライブラリと例をコンパイルだけする (プロジェクト本体はリンクしない)
#   test/arm_build_check.sh <CubeMX プロジェクトのフォルダ> link <リンク確認用の .c>
#       .c を足してプロジェクト全体をリンクし、HAL のコールバックがライブラリのものになっているか確かめる
set -euo pipefail

lib_dir=$(cd "$(dirname "$0")/.." && pwd)
src=$(cd "$1" && pwd)
mode=$2
name=$(basename "$src")
work=$(mktemp -d "${TMPDIR:-/tmp}/mor0koshi_check.XXXXXX")
proj="$work/$name"

cp -a "$src" "$proj"
rm -rf "$proj/build" "$proj/.git"

{
    echo ''
    echo '# ---- mor0koshi_library のビルド確認 (arm_build_check.sh が追加) ----'
    echo 'set(MOR0KOSHI_BUILD_EXAMPLES ON CACHE BOOL "" FORCE)'
    echo "add_subdirectory(\"$lib_dir\" mor0koshi_library)"
    if [ "$mode" = link ]; then
        cp "$3" "$proj/linkcheck.c"
        echo 'target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/linkcheck.c)'
        echo 'target_link_libraries(${CMAKE_PROJECT_NAME} mor0koshi_library)'
        # 呼ばれていない関数も残して、どのコールバックが選ばれたかを nm で見られるようにする
        echo 'target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,--no-gc-sections)'
    fi
} >> "$proj/CMakeLists.txt"

cd "$proj"
cmake --preset Debug > /dev/null
if [ "$mode" = link ]; then
    cmake --build build/Debug 2>&1 | tee build.log
    cmake --build build/Debug --target mor0koshi_examples 2>&1 | tee -a build.log
else
    cmake --build build/Debug --target mor0koshi_library mor0koshi_examples 2>&1 | tee build.log
fi

if grep -E "mor0koshi_library/[^:]*:[0-9]+:[0-9]+: warning" build.log; then
    echo "NG: ライブラリか例に警告があります"
    exit 1
fi

if [ "$mode" = link ]; then
    elf=$(ls build/Debug/*.elf)
    for sym in HAL_CAN_RxFifo0MsgPendingCallback HAL_CAN_TxMailbox0CompleteCallback \
               HAL_UARTEx_RxEventCallback HAL_UART_ErrorCallback; do
        line=$(arm-none-eabi-nm "$elf" | awk -v s="$sym" '$3 == s')
        if [ -z "$line" ]; then
            echo "$sym: (無し)"
            continue
        fi
        addr=$(echo "$line" | awk '{print $1}')
        type=$(echo "$line" | awk '{print $2}')
        where=$(arm-none-eabi-addr2line -e "$elf" "0x$addr")
        echo "$sym: $type $where"
        case "$where" in
            *mor0koshi_library/src/*) ;;
            *) echo "NG: $sym が HAL の空の関数のまま (ライブラリのものになっていない)"; exit 1 ;;
        esac
    done
fi

echo "OK: $name ($mode) $proj"
```

`test/linkcheck_f446.c` (2026_B_446 用。CAN・TIM・UART を全部使う):

```c
/* arm_build_check.sh の link で足すファイル。ライブラリの全部品をリンクさせる (実行はしない) */
#include "mor0koshi_library.h"

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
```

`test/linkcheck_nocan.c` (mortor_test 用。CAN が無効のプロジェクト):

```c
/* arm_build_check.sh の link で足すファイル。CAN が無効のプロジェクトで、SBUS とエンコーダーだけ使う */
#include "mor0koshi_library.h"

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
```

```bash
chmod +x "/home/fukui-ros/STM32 project/mor0koshi_library/test/arm_build_check.sh"
```

- [ ] **Step 3: F767 (CAN あり) でコンパイルを確かめる**

```bash
cd "/home/fukui-ros/STM32 project/mor0koshi_library"
test/arm_build_check.sh "../2026_B_767" compile
```

Expected: 最後に `OK: 2026_B_767 (compile) ...`。`NG:` が出たら、表示された警告やエラーを直してからやり直す。

- [ ] **Step 4: F446 (CAN あり) でリンクまで確かめる**

```bash
cd "/home/fukui-ros/STM32 project/mor0koshi_library"
test/arm_build_check.sh "../2026_B_446" link test/linkcheck_f446.c
```

Expected: 4 つのコールバックがすべて `T .../mor0koshi_library/src/mor0koshi_can.c:NN` または `.../mor0koshi_sbus.c:NN` と表示され、最後に `OK: 2026_B_446 (link) ...`。`W` (weak) や HAL のファイル名が出たら、静的ライブラリの強い定義が HAL の weak を上書きできていないので、`CMakeLists.txt` のリンクのしかたを見直す。

- [ ] **Step 5: F767 (CAN なし) でリンクまで確かめる**

```bash
cd "/home/fukui-ros/STM32 project/mor0koshi_library"
test/arm_build_check.sh "../mortor_test" link test/linkcheck_nocan.c
```

Expected: CAN の 2 つは `(無し)`、UART の 2 つは `T .../mor0koshi_sbus.c:NN`、最後に `OK: mortor_test (link) ...`。

- [ ] **Step 6: PC のテストも通ることを確かめる**

Run: (テストの走らせ方)
Expected: `100% tests passed, 0 tests failed out of 6`

- [ ] **Step 7: コミット**

```bash
cd "/home/fukui-ros/STM32 project/mor0koshi_library"
git add CMakeLists.txt examples test/arm_build_check.sh test/linkcheck_f446.c test/linkcheck_nocan.c
git commit -F - <<'EOF'
使う例 3 つと、ARM でのビルド確認のスクリプトを追加

例: プロポでロボマスを回す / エンコーダーを CAN で送る / 受け取る。
arm_build_check.sh は CubeMX のプロジェクトを一時フォルダにコピーしてビルドし、
HAL のコールバックがライブラリのものになっているかも確かめる。
F767 (2026_B_767)・F446 (2026_B_446)・CAN なしの F767 (mortor_test) で確認済み。

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task 8: README

**Files:**
- Create: `README.md`

**Interfaces:**
- Consumes: Task 1〜7 の公開関数・例・スクリプト (名前は各タスクの Produces のとおり)
- Produces: なし

- [ ] **Step 1: README を書く**

`README.md`:

````markdown
# mor0koshi_library

STM32 (HAL) のロボット用ライブラリです。次の 4 つを、`init` を呼ぶだけで使えるようにします。

| 部品 | できること |
|---|---|
| **CAN** (`can_*`) | 好きな ID の送受信。受信の振り分け、送信待ちの列、断線の判定 |
| **SBUS** (`sbus_*`) | プロポの受信。スティック (±1000)・スイッチの変換、断線とフェイルセーフの判定 |
| **エンコーダー** (`encoder_*`) | タイマーのエンコーダーモードで累積カウント・rpm |
| **ロボマス** (`robomas_*`) | M3508 (C620) / M2006 (C610) の電流・速度・位置制御 |

おまけで、エンコーダーの値を CAN で別の基板に送る `encoder_can_*` もあります。

- 対象: bxCAN を持つ STM32 F4 / F7 (F446・F767 で確認)。G4 / H7 の FDCAN には対応していません
- 言語: C11。malloc は使いません

## プロジェクトへの組み込み

このフォルダを CubeMX のプロジェクトの隣に置き (`STM32 project/mor0koshi_library`)、
プロジェクトの `CMakeLists.txt` に 2 か所足します。

```cmake
# 「Add STM32CubeMX generated sources」の add_subdirectory(cmake/stm32cubemx) の下に
add_subdirectory(../mor0koshi_library mor0koshi_library)

# 「Add linked libraries」に足す
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    mor0koshi_library
)
```

使うファイルで `#include "mor0koshi_library.h"` すれば全部使えます。

- CubeMX で有効にしていない機能 (CAN など) の部品は、自動でビルドから外れます
- 呼んだ部品だけがリンクされます (`sbus_init` を呼ばなければ SBUS の部分は入りません)

## 使い方

### CAN

CubeMX: CAN を有効にして通信速度を決め、**NVIC で RX0 と TX の割り込みをオン**にします。
`HAL_CAN_Start` とフィルタの設定は自分で書きません (`can_init` がやります)。

```c
can_t can1;
can_init(&can1, &hcan1);

// 送る (標準 ID、0〜8 バイト)
uint8_t d[8];
can_put_i16(d, 0, 1500);      // d[0..1] に int16 (リトルエンディアン)
can_put_float(d, 2, 3.14f);   // d[2..5] に float
can_send(&can1, 0x123, d, 8);

// 受け取る (最新の 1 フレームを取っておく箱)
can_box_t box;
can_listen(&can1, 0x456, &box);
if (can_read(&box, d)) {      // 新しく届いていれば true
    int16_t v = can_get_i16(d, 0);
}
if (can_age_ms(&box) > 100) { /* 100ms 届いていない = 断線 */ }

// 届いた瞬間に関数を呼ぶ (割り込みの中で動くので短く書く)
void on_rx(uint32_t id, const uint8_t *data, uint8_t len, void *arg) { ... }
can_subscribe(&can1, 0x456, on_rx, NULL);
```

- 送信のメールボックスは 3 つしかないので、ライブラリは 16 フレームの待ち列を持っています。
  列があふれると `can_send` が `LIB_ERR_FULL` を返します (ケーブルが抜けているときなど)
- 登録 (`can_listen`・`can_subscribe`) は 1 本の CAN につき 16 個までです
- ケーブルが抜けてバスオフになっても、自動で復帰します

### SBUS

CubeMX: UART を **100000bps・9bit (Even パリティ)・ストップビット 2** にし、**RX の DMA (Normal)** と
**UART の割り込み**をオンにします。F7 は Advanced Features の **RX Pin Active Level Inversion** もオンにします。
F4 の UART には反転の機能がないので、基板にインバーターが要ります。

```c
sbus_t sbus;
sbus_init(&sbus, &huart5);

// メインループで毎周期 1 回
sbus_update(&sbus);
if (!sbus_ok(&sbus)) { /* 全部止める */ }
int ly  = sbus_stick(&sbus, 1);     // -1000〜+1000 (中立付近は 0)
int sw3 = sbus_switch3(&sbus, 6);   // -1 / 0 / +1 (生の値が大きいほうが +1)
int sw2 = sbus_switch2(&sbus, 4);   // 0 / 1
int raw = sbus_raw(&sbus, 0);       // 生の値 (0〜2047)
```

`sbus_ok` が false になるのは、1 フレームも来ていない・200ms 以上来ていない (コネクタ抜け)・
フェイルセーフ (送信機の電源 OFF) のときです。このときスティックとスイッチは 0 を返します。

> ⚠ **スイッチの押し直し**: 「スイッチを OFF にしたら解除」のような判定は、`sbus_ok` が true のときだけ
> OFF とみなしてください。NG の間はスイッチが 0 (OFF) と読めるので、それを OFF と扱うと
> ON のままでも解除されてしまいます。

送信機によって値の範囲が違うときは調整します (初期値は 2026_B_767 の値)。

```c
sbus_set_stick_range(&sbus, 368, 1024, 1680);  // 最小・中立・最大
sbus_set_deadband(&sbus, 26);                  // 中立から ±26 以内は 0
```

スイッチの境目は、この範囲の中間点 (中立と端の真ん中) から自動で決まります。

### エンコーダー

CubeMX: タイマーを **Encoder Mode (TI1 and TI2)**、Prescaler 0、Period を最大
(16bit なら 65535、TIM2 / TIM5 なら 4294967295) にします。割り込みは要りません。

```c
encoder_t enc1;
encoder_init(&enc1, &htim1, 2048);   // 2048 = 1 回転のカウント数 (512 パルス × 4 逓倍)
encoder_set_reverse(&enc1, true);    // 向きを逆にしたいとき

// 決まった周期 (1〜10ms) で呼ぶ
encoder_update(&enc1);
int32_t c = encoder_count(&enc1);    // 起動からの累積カウント
int32_t d = encoder_delta(&enc1);    // 前回の update からの変化量
float rpm = encoder_rpm(&enc1);      // 回転数 (符号付き)
float rev = encoder_turns(&enc1);    // 累積の回転数
encoder_reset(&enc1);                // 累積を 0 に戻す
```

- rpm は `update` を呼んだ間隔を CPU のサイクルカウンタで測って計算します。周期を指定する必要はありません
- 遅い回転を 1ms 周期で測ると粗くなります (2048 カウントなら 1 カウント ≒ 29rpm)。10ms 周期くらいがおすすめです

### エンコーダーの値を CAN で送る

エンコーダー 1 個を 1 フレーム (`[0..3]` int32 累積カウント、`[4..7]` float rpm) で送ります。

```c
// 送る側 (10ms ごと)
encoder_can_send(&can1, 0x300, &enc1);

// 受ける側
encoder_can_rx_t roller1;
encoder_can_listen(&can1, 0x300, &roller1);
float rpm = encoder_can_rpm(&roller1);
int32_t c = encoder_can_count(&roller1);
if (encoder_can_age_ms(&roller1) > 100) { /* 断線 */ }
```

### ロボマス (M3508 / M2006)

CAN を 1Mbps にして `can_init` します。ESC の ID は本体のボタンで 1〜8 に設定しておきます。

```c
robomas_t rm;
robomas_init(&rm, &can1);
robomas_motor_t *wheel = robomas_add(&rm, ROBOMAS_M3508, 1);
robomas_motor_t *arm   = robomas_add(&rm, ROBOMAS_M2006, 5);

robomas_set_rpm(wheel, 300.0f);   // 速度制御: 出力軸 rpm
robomas_set_angle(arm, 90.0f);    // 位置制御: 出力軸の角度 [度] (720 なら 2 回転)
robomas_set_current(wheel, 2000); // 電流を直接 (M3508: ±16384 / M2006: ±10000)

// 1ms ごとに 1 回
robomas_update(&rm);

float rpm = robomas_rpm(wheel);   // 出力軸 rpm
float deg = robomas_angle(arm);   // 出力軸の累積角度 (起動時の位置が 0 度)
robomas_reset_angle(arm);         // 今の位置を 0 度に
bool ok = robomas_ok(wheel);      // フィードバックが届いているか

if (!sbus_ok(&sbus)) robomas_stop_all(&rm);  // プロポが切れたら止める (update は呼び続ける)
```

- 値は全部**出力軸** (ギアの先) です。ギア比は種類から自動で入ります (M3508 ≒ 19.2、M2006 = 36)
- フィードバックが 100ms 届かないモーターは、自動で出力 0 になります

## HAL のコールバックについて

ライブラリは次の HAL のコールバックを自分で持っています。**同じ関数を自分で書くとリンクエラー
(multiple definition) になります。**

| 部品を使うと | ライブラリが持つコールバック |
|---|---|
| `can_init` | `HAL_CAN_RxFifo0MsgPendingCallback`、`HAL_CAN_TxMailbox0/1/2CompleteCallback`、`HAL_CAN_TxMailbox0/1/2AbortCallback` |
| `sbus_init` | `HAL_UARTEx_RxEventCallback`、`HAL_UART_ErrorCallback` |

SBUS 以外の UART (Lidar など) の受信やエラーを扱いたいときは、`HAL_UARTEx_RxEventCallback` /
`HAL_UART_ErrorCallback` の代わりに、次の関数を書いてください。ライブラリが使わない UART の分はここに来ます。

```c
void uart_rx_event_hook(UART_HandleTypeDef *huart, uint16_t size) { ... }
void uart_error_hook(UART_HandleTypeDef *huart) { ... }
```

CAN の FIFO1 と `HAL_CAN_ErrorCallback` はライブラリが使わないので、自由に書けます。

## 名前がかぶったとき

関数名に頭の印を付けていないので、自分のコードに同じ名前 (`can_send`、`encoder_init` など) があると
リンクエラー (multiple definition) になります。どちらかの名前を変えてください。

## エラーの調べ方

`init`・`send` などは `lib_status_t` を返します。

| 値 | 意味 |
|---|---|
| `LIB_OK` | 成功 |
| `LIB_ERR_PARAM` | 引数がおかしい (ID が範囲外、同じ ID を二重に登録、NULL など) |
| `LIB_ERR_FULL` | 登録の表か送信待ちの列がいっぱい |
| `LIB_ERR_HAL` | HAL の関数が失敗した (`HAL_CAN_Start` を自分でも呼んでいる、など) |
| `LIB_ERR_IRQ_OFF` | CubeMX で必要な割り込みや DMA がオフ |

失敗するたびに `lib_error_hook` が呼ばれるので、書いておくと原因がすぐ分かります。

```c
void lib_error_hook(lib_status_t err, const char *where) {
    printf("lib error %d in %s\n", err, where);
}
```

CAN が抜けているときは 1ms ごとに呼ばれることがあるので、重い処理は書かないでください。

「つながっているのに動かない」ときは、`can_dropped_count` (送信で捨てた数)、`sbus_frame_count`
(受信したフレームの数)、`sbus_error_count` (UART エラーの数) を見ると切り分けられます。

## CAN の通信量の目安

1Mbps の CAN で 8 バイトのフレーム 1 つは約 0.13ms です。

- ロボマスは 1 台ごとに 1ms ごとにフィードバックを返すので、**1 台で約 13%** を使います。
  **1 本の CAN にはおよそ 5 台まで**にし、それより多いときは CAN2 に分けてください
- エンコーダーを 6 個 1ms ごとに送ると約 8 割になります。**10ms ごと (約 8%)** がおすすめです
- 自分で決める ID は、ロボマス (0x1FF〜0x2FF) とぶつからない **0x300 番台**がおすすめです

## 実機での確認

新しい基板やプロジェクトで初めて使うときは、次の順に確かめてください。

- [ ] **SBUS**: `sbus_raw` の値を表示し、スティックを動かすと変わる
- [ ] **SBUS**: 受信機のコネクタを抜くと、200ms 以内に `sbus_ok` が false になる
- [ ] **SBUS**: 送信機の電源を切ると `sbus_ok` が false になる
- [ ] **CAN**: 2 枚の基板で `can_send` と `can_listen` をして、値が届く。片方の電源を切ると `can_age_ms` が増え続ける
- [ ] **エンコーダー**: 手で 1 回転させると `encoder_turns` がおよそ 1.0 (逆なら `encoder_set_reverse`)
- [ ] **ロボマス**: タイヤを浮かせて、`robomas_set_rpm` に小さい値 (10rpm くらい) を入れると、その速さで回る
- [ ] **ロボマス**: `robomas_set_angle(m, 360)` で出力軸が 1 回転して止まる
- [ ] **ロボマス**: プロポを切ると止まる (`robomas_stop_all`)

## ゲインの調整 (ロボマス)

初期値は次のとおりです。**M2006 の速度ゲイン以外は実機で確かめていない仮の値**なので、調整してください。

| | 速度 kp / ki / 積分上限 | 位置 kp / 上限 rpm |
|---|---|---|
| M3508 | 1.5 / 0.08 / 5000 (仮) | 2.0 / 100 (仮) |
| M2006 | 1.5 / 0.08 / 5000 (AIcontest で確認済み) | 2.0 / 100 (仮) |

```c
robomas_set_speed_gain(m, 1.5f, 0.08f, 5000.0f);  // kp, ki, 積分上限
robomas_set_angle_gain(m, 2.0f, 100.0f);          // kp (rpm / 度), 上限 rpm (出力軸)
```

1. 速度から合わせます。`robomas_set_rpm` で目標を出し、`robomas_rpm` を見ながら kp を上げます (振動したら下げる)
2. 目標に届かずに残る差は、ki を少しずつ上げて消します
3. 速度が合ったら位置です。`robomas_set_angle` で動かし、行き過ぎるなら位置の kp か上限 rpm を下げます

ゲインは「1ms ごとに `robomas_update`」が前提です。周期を変えると積分の効き方が変わります。

## 開発者向け

### PC でのテスト

HAL の偽物 (`test/fake/`) を使って、PC の gcc でライブラリをそのままテストします。

```bash
cmake -S test -B build-test -G Ninja && cmake --build build-test && ctest --test-dir build-test --output-on-failure
```

### ARM でのビルド確認

本物の CubeMX プロジェクトを一時フォルダにコピーして、その設定でビルドします (元のプロジェクトには触りません)。

```bash
test/arm_build_check.sh ../2026_B_767 compile
test/arm_build_check.sh ../2026_B_446 link test/linkcheck_f446.c
test/arm_build_check.sh ../mortor_test link test/linkcheck_nocan.c
```

設計の詳しいことは `docs/specs/2026-10-20-mor0koshi-library-design.md` にあります。
````

- [ ] **Step 2: README のコードが実際の名前と合っているか確かめる**

```bash
cd "/home/fukui-ros/STM32 project/mor0koshi_library"
for f in $(grep -oE '\b(can|sbus|encoder|encoder_can|robomas|uart|lib)_[a-z_0-9]+\(' README.md | tr -d '(' | sort -u); do
  grep -q "\b$f(" include/*.h || echo "README にあってヘッダーに無い: $f"
done
```

Expected: 何も表示されない (表示されたら README を直す)

- [ ] **Step 3: コミット**

```bash
cd "/home/fukui-ros/STM32 project/mor0koshi_library"
git add README.md
git commit -F - <<'EOF'
README を追加

組み込み方、部品ごとの CubeMX の設定と使い方、HAL のコールバックの注意、
エラーの調べ方、CAN の通信量の目安、実機での確認手順、ゲインの調整を書いた。

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```
