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
    uint32_t pm = port_irq_save(); /* 優先度の高い割り込みから can_send が呼ばれても列が壊れないように */
    can_t *bus = find_bus(hcan);
    if (bus != NULL) {
        tx_pump(bus);
    }
    port_irq_restore(pm);
}

void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef *hcan) { on_tx_done(hcan); }
void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef *hcan) { on_tx_done(hcan); }
void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef *hcan) { on_tx_done(hcan); }
void HAL_CAN_TxMailbox0AbortCallback(CAN_HandleTypeDef *hcan) { on_tx_done(hcan); }
void HAL_CAN_TxMailbox1AbortCallback(CAN_HandleTypeDef *hcan) { on_tx_done(hcan); }
void HAL_CAN_TxMailbox2AbortCallback(CAN_HandleTypeDef *hcan) { on_tx_done(hcan); }

#endif /* HAL_CAN_MODULE_ENABLED */
