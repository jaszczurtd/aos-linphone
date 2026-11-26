/* Entropy source for mbedtls built with MBEDTLS_ENTROPY_HARDWARE_ALT
   (deps/mbedtls/aros_user_config.h). */
#include <stddef.h>
#include <unistd.h>

/* Same prototype as in mbedtls/entropy_poll.h. */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;
    *olen = 0;
    /* getentropy() hands out at most 256 bytes per call. */
    while (len > 0) {
        size_t chunk = len > 256 ? 256 : len;
        if (getentropy(output, chunk) != 0) {
            return -1; /* the caller treats any non-zero value as a failure */
        }
        output += chunk;
        len -= chunk;
        *olen += chunk;
    }
    return 0;
}
