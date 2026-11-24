/*
 * Minimal test helpers. Each check prints one PASS/FAIL line; a test
 * program ends with aos_test_result(), whose value is the AROS return code
 * (0 when every check passed, 10 otherwise).
 */
#ifndef AOS_TEST_H
#define AOS_TEST_H

#include <stdio.h>
#include <time.h>

static int aos_test_failures;

#define AOS_CHECK(cond, what)                                          \
    do {                                                               \
        int aos_ok_ = (cond) ? 1 : 0;                                  \
        printf("%s: %s\n", aos_ok_ ? "PASS" : "FAIL", (what));         \
        if (!aos_ok_) {                                                \
            printf("      at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++aos_test_failures;                                       \
        }                                                              \
    } while (0)

static inline int aos_test_result(void) {
    printf("result: %s\n", aos_test_failures ? "FAIL" : "PASS");
    return aos_test_failures ? 10 : 0;
}

static inline long aos_test_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static inline void aos_test_sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

#endif /* AOS_TEST_H */
