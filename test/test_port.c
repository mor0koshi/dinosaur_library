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
