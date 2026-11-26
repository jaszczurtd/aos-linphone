/*
 * __cxa_atexit() and __dso_handle for C++ built with -fuse-cxa-atexit.
 *
 * GCC for MorphOS defaults to -fno-use-cxa-atexit, and then never registers
 * the destructors of thread_local objects: they do not run when a thread
 * ends. With -fuse-cxa-atexit (the MorphOS platform files add it to every
 * C++ compile), GCC registers them through __cxa_thread_atexit() of
 * libstdc++, which works with libnix, and registers the destructors of
 * static objects through __cxa_atexit(), which libnix lacks, like
 * __dso_handle.
 *
 * Each registration takes one of a fixed set of trampolines and hands it to
 * atexit(), so a static destructor runs in the same place among the other
 * atexit() handlers as with -fno-use-cxa-atexit. Registrations beyond that
 * set go to one list that a single atexit() handler runs in reverse order.
 * Programs are statically linked, so there is one DSO and __cxa_finalize()
 * is not needed.
 */
#include <pthread.h>
#include <stdlib.h>

void *__dso_handle = &__dso_handle;

int __cxa_atexit(void (*func)(void *), void *obj, void *dso);

struct aos_cxa_entry {
    void (*func)(void *);
    void *obj;
};

/* 0x000 to 0x3ff: 1024 trampolines. */
#define AOS_CXA_SLOTS 1024
#define AOS_CXA_EXTRA 256

static struct aos_cxa_entry slots[AOS_CXA_SLOTS];
static unsigned int slots_used = 0;
static struct aos_cxa_entry extra[AOS_CXA_EXTRA];
static unsigned int extra_used = 0;
static pthread_mutex_t cxa_lock = PTHREAD_MUTEX_INITIALIZER;

#define AOS_T(n)                             \
    static void aos_cxa_##n(void) {          \
        slots[0x##n].func(slots[0x##n].obj); \
    }
#define AOS_T16(n)                                                                                  \
    AOS_T(n##0)                                                                                     \
    AOS_T(n##1) AOS_T(n##2) AOS_T(n##3) AOS_T(n##4) AOS_T(n##5) AOS_T(n##6) AOS_T(n##7) AOS_T(n##8) \
        AOS_T(n##9) AOS_T(n##a) AOS_T(n##b) AOS_T(n##c) AOS_T(n##d) AOS_T(n##e) AOS_T(n##f)
#define AOS_T256(n)                                                                                       \
    AOS_T16(n##0)                                                                                         \
    AOS_T16(n##1) AOS_T16(n##2) AOS_T16(n##3) AOS_T16(n##4) AOS_T16(n##5) AOS_T16(n##6)                   \
        AOS_T16(n##7) AOS_T16(n##8) AOS_T16(n##9) AOS_T16(n##a) AOS_T16(n##b) AOS_T16(n##c) AOS_T16(n##d) \
            AOS_T16(n##e) AOS_T16(n##f)

/* clang-format off */
AOS_T256(0) AOS_T256(1) AOS_T256(2) AOS_T256(3)
/* clang-format on */

#define AOS_P(n) aos_cxa_##n,
#define AOS_P16(n)                                                                                  \
    AOS_P(n##0)                                                                                     \
    AOS_P(n##1) AOS_P(n##2) AOS_P(n##3) AOS_P(n##4) AOS_P(n##5) AOS_P(n##6) AOS_P(n##7) AOS_P(n##8) \
        AOS_P(n##9) AOS_P(n##a) AOS_P(n##b) AOS_P(n##c) AOS_P(n##d) AOS_P(n##e) AOS_P(n##f)
#define AOS_P256(n)                                                                                       \
    AOS_P16(n##0)                                                                                         \
    AOS_P16(n##1) AOS_P16(n##2) AOS_P16(n##3) AOS_P16(n##4) AOS_P16(n##5) AOS_P16(n##6)                   \
        AOS_P16(n##7) AOS_P16(n##8) AOS_P16(n##9) AOS_P16(n##a) AOS_P16(n##b) AOS_P16(n##c) AOS_P16(n##d) \
            AOS_P16(n##e) AOS_P16(n##f)

    static void (*const trampolines[AOS_CXA_SLOTS])(void) = {AOS_P256(0) AOS_P256(1) AOS_P256(2) AOS_P256(3)};

static void run_extra(void) {
    while (extra_used > 0) {
        --extra_used;
        extra[extra_used].func(extra[extra_used].obj);
    }
}

int __cxa_atexit(void (*func)(void *), void *obj, void *dso) {
    int result = -1;
    (void)dso;
    if (func == NULL) {
        return -1;
    }
    pthread_mutex_lock(&cxa_lock);
    if (slots_used < AOS_CXA_SLOTS) {
        unsigned int slot = slots_used;
        slots[slot].func = func;
        slots[slot].obj = obj;
        if (atexit(trampolines[slot]) == 0) {
            ++slots_used;
            result = 0;
        }
    } else if (extra_used < AOS_CXA_EXTRA) {
        if (extra_used > 0 || atexit(run_extra) == 0) {
            extra[extra_used].func = func;
            extra[extra_used].obj = obj;
            ++extra_used;
            result = 0;
        }
    }
    pthread_mutex_unlock(&cxa_lock);
    return result;
}
