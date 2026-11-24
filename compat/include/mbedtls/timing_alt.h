/*
 * mbedtls/timing_alt.h - timer types for mbedtls built with
 * MBEDTLS_TIMING_ALT (deps/mbedtls/aros_user_config.h). mbedtls' own
 * timing.c accepts only Unix and Windows; libaoscompat implements the
 * functions over clock_gettime(CLOCK_MONOTONIC) with the semantics of the
 * generic mbedtls implementation.
 */
#ifndef AOS_MBEDTLS_TIMING_ALT_H
#define AOS_MBEDTLS_TIMING_ALT_H

#include <stdint.h>
#include <time.h>

/** Start time of a timer. */
struct mbedtls_timing_hr_time {
    struct timespec start;
};

/** Context of the DTLS retransmission timer. */
typedef struct mbedtls_timing_delay_context {
    struct mbedtls_timing_hr_time timer;
    uint32_t int_ms; /* intermediate delay in milliseconds */
    uint32_t fin_ms; /* final delay in milliseconds, 0 when cancelled */
} mbedtls_timing_delay_context;

#endif /* AOS_MBEDTLS_TIMING_ALT_H */
