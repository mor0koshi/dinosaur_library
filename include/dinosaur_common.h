#ifndef DINOSAUR_COMMON_H
#define DINOSAUR_COMMON_H

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
 * 割り込みの中から呼ばれることもある (SBUS のコールバック、割り込みの中の can_send)。
 */
void lib_error_hook(lib_status_t err, const char *where);

#ifdef __cplusplus
}
#endif

#endif
