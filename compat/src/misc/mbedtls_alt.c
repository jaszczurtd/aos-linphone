/*
 * Platform functions for mbedtls built with MBEDTLS_PLATFORM_MS_TIME_ALT
 * and MBEDTLS_TIMING_ALT (deps/mbedtls/aros_user_config.h): mbedtls does
 * not recognise AROS as Unix-like, so it has no clock of its own here.
 * The prototypes are those of mbedtls/platform_time.h and mbedtls/timing.h.
 */
#include "mbedtls/timing_alt.h"

#include <stdint.h>
#include <time.h>

int64_t mbedtls_ms_time(void);
unsigned long mbedtls_timing_get_timer(struct mbedtls_timing_hr_time *val, int reset);
void mbedtls_timing_set_delay(void *data, uint32_t int_ms, uint32_t fin_ms);
int mbedtls_timing_get_delay(void *data);
uint32_t mbedtls_timing_get_final_delay(const mbedtls_timing_delay_context *data);

int64_t mbedtls_ms_time(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return (int64_t)time(NULL) * 1000;
    }
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

unsigned long mbedtls_timing_get_timer(struct mbedtls_timing_hr_time *val, int reset)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (reset) {
        val->start = now;
        return 0;
    }
    return (unsigned long)((now.tv_sec - val->start.tv_sec) * 1000L +
                           (now.tv_nsec - val->start.tv_nsec) / 1000000L);
}

void mbedtls_timing_set_delay(void *data, uint32_t int_ms, uint32_t fin_ms)
{
    mbedtls_timing_delay_context *ctx = data;
    ctx->int_ms = int_ms;
    ctx->fin_ms = fin_ms;
    if (fin_ms != 0) {
        (void)mbedtls_timing_get_timer(&ctx->timer, 1);
    }
}

/* -1 cancelled, 0 no delay passed, 1 intermediate passed, 2 final passed. */
int mbedtls_timing_get_delay(void *data)
{
    mbedtls_timing_delay_context *ctx = data;
    if (ctx->fin_ms == 0) {
        return -1;
    }
    unsigned long elapsed_ms = mbedtls_timing_get_timer(&ctx->timer, 0);
    if (elapsed_ms >= ctx->fin_ms) {
        return 2;
    }
    if (elapsed_ms >= ctx->int_ms) {
        return 1;
    }
    return 0;
}

uint32_t mbedtls_timing_get_final_delay(const mbedtls_timing_delay_context *data)
{
    return data->fin_ms;
}
