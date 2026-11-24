/* backtrace() family without unwinder support. */
#include <execinfo.h>

#include <stddef.h>

int backtrace(void **buffer, int size) {
    (void)buffer;
    (void)size;
    return 0;
}

char **backtrace_symbols(void *const *buffer, int size) {
    (void)buffer;
    (void)size;
    return NULL;
}

void backtrace_symbols_fd(void *const *buffer, int size, int fd) {
    (void)buffer;
    (void)size;
    (void)fd;
}
