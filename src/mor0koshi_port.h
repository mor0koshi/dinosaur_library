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
