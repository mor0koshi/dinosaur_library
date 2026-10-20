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
