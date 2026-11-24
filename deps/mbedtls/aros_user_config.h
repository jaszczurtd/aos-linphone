/*
 * Adjustments to the mbedtls configuration of linphone-sdk for AROS, read by
 * mbedtls after its own configuration (MBEDTLS_USER_CONFIG_FILE).
 */
#ifndef AOS_MBEDTLS_USER_CONFIG_H
#define AOS_MBEDTLS_USER_CONFIG_H

/* mbedtls knows no AROS entropy source (entropy_poll.c stops with #error);
   libaoscompat provides mbedtls_hardware_poll() over posixc getentropy(). */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

/* mbedtls takes its clocks from Unix or Windows only (platform_util.c,
   timing.c). libaoscompat provides mbedtls_ms_time() and the DTLS timer
   functions; the timer types are in mbedtls/timing_alt.h of the
   compatibility layer, installed next to the mbedtls headers. */
#define MBEDTLS_PLATFORM_MS_TIME_ALT
#define MBEDTLS_TIMING_ALT

/* bctoolbox does its own socket I/O; mbedtls' net_sockets.c would need the
   AROS socket wrapper. */
#undef MBEDTLS_NET_C

#endif /* AOS_MBEDTLS_USER_CONFIG_H */
