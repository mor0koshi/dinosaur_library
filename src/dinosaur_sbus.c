/*
 * SBUS の受信。2026_B_767 の sbus_handler.c を元にしている (実機確認済みの判定をそのまま使う)。
 * UART がアイドルになる (1 フレーム受け終わる) たびに、バッファから 0x0F を探して 25 バイトをデコードする。
 */
#include "dinosaur_sbus.h"

#include <string.h>

#include "dinosaur_port.h"

#if defined(HAL_UART_MODULE_ENABLED)

#define SBUS_FRAME_LEN 25
#define SBUS_HEADER 0x0FU
#define SBUS_END_MASK 0x0FU /* SBUS2 の末尾は 0x04 / 0x14 / 0x24 / 0x34 (下位 4bit が 4) */
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

/* huart を別の sbus_t がすでに持っているか。割り込みを止めて呼ぶこと */
static bool owned_by_other(const sbus_t *s, const UART_HandleTypeDef *huart) {
    for (int i = 0; i < SBUS_MAX_PORTS; i++) {
        if (ports[i] != NULL && ports[i] != s && ports[i]->huart == huart) {
            return true;
        }
    }
    return false;
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
    if (huart->hdmarx->Init.Mode == DMA_CIRCULAR) {
        /* Circular だと受信をかけ直せず、Size も増え続ける位置になり、古いフレームを読み続ける。Normal にする */
        return port_fail(LIB_ERR_IRQ_OFF, "sbus_init");
    }
    uint32_t pm = port_irq_save();
    if (owned_by_other(s, huart)) {
        /* 別の sbus_t で同じ UART を初期化すると、先の sbus_t の登録が消えてしまう。memset の前に断る */
        port_irq_restore(pm);
        return port_fail(LIB_ERR_PARAM, "sbus_init");
    }
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
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    /* memset で汚れたキャッシュの行が、あとで DMA の書いた値を上書きしないように、受信の前に書き戻して捨てる */
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        SCB_CleanInvalidateDCache_by_Addr((uint32_t *)(void *)s->rxbuf, SBUS_RXBUF_LEN);
    }
#endif
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
        /*
         * データの途中の 0x0F をヘッダーと間違えないように、末尾のバイトも確かめる。
         * 末尾は 0x00 (SBUS) か 下位 4bit が 4 (SBUS2)。合わなければ次の 0x0F を探す。
         */
        uint8_t end = s->rxbuf[i + SBUS_FRAME_LEN - 1];
        if (s->rxbuf[i] == SBUS_HEADER && (end == 0x00U || (end & SBUS_END_MASK) == 0x04U)) {
            decode(s, &s->rxbuf[i]);
            break;
        }
    }
    /* 受信をかけ直せないと、それ以降ずっと受信が止まるので知らせる */
    if (start_rx(s) != HAL_OK) {
        lib_error_hook(LIB_ERR_HAL, "HAL_UARTEx_RxEventCallback");
    }
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
    /* 受信をかけ直せないと、それ以降ずっと受信が止まるので知らせる */
    if (start_rx(s) != HAL_OK) {
        lib_error_hook(LIB_ERR_HAL, "HAL_UART_ErrorCallback");
    }
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
