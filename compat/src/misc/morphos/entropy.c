/* Entropy source for mbedtls built with MBEDTLS_ENTROPY_HARDWARE_ALT
   (deps/mbedtls/aros_user_config.h): RandomBytes() of random.library (V2),
   which gathers entropy from system events. libauto opens the library. */
#include <stddef.h>

#include <proto/random.h>

/* Same prototype as in mbedtls/entropy_poll.h. */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;
    *olen = 0;
    while (len > 0) {
        LONG chunk = len > 0x10000 ? 0x10000 : (LONG)len;
        RandomBytes(output, chunk);
        output += chunk;
        len -= (size_t)chunk;
        *olen += (size_t)chunk;
    }
    return 0;
}
