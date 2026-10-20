/* ライブラリ全体で使うもの */
#include "dinosaur_common.h"

/* 使う側が同じ名前の関数を書けば、そちらが使われる */
__attribute__((weak)) void lib_error_hook(lib_status_t err, const char *where) {
    (void)err;
    (void)where;
}
