/* エンコーダーの値を CAN で送る・受け取る。1 個 = 1 フレーム ([0..3] 累積カウント、[4..7] rpm) */
#include "dinosaur_encoder_can.h"

#include "dinosaur_port.h"

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
