/*
 * execinfo.h - stack backtrace interface for AROS, implemented by
 * libaoscompat. No backtrace is available: backtrace() reports an empty
 * stack. bctoolbox includes this header on every Unix build.
 */
#ifndef AOS_EXECINFO_H
#define AOS_EXECINFO_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Captures no frames. @return 0. */
int backtrace(void **buffer, int size);

/** @brief No symbol names are available. @return NULL. */
char **backtrace_symbols(void *const *buffer, int size);

/** @brief Writes nothing. */
void backtrace_symbols_fd(void *const *buffer, int size, int fd);

#ifdef __cplusplus
}
#endif

#endif /* AOS_EXECINFO_H */
