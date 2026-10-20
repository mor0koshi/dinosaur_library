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
