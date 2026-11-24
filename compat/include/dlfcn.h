/*
 * dlfcn.h - dynamic loading interface for AROS, implemented by libaoscompat.
 *
 * AROS programs are linked statically against the linphone stack and load
 * no ELF modules at run time, so these calls always fail: dlopen() and
 * dlsym() return NULL and dlerror() describes why. They exist because the
 * stack calls them unconditionally (for optional plugins and a debug
 * helper) and handles the failure.
 */
#ifndef AOS_DLFCN_H
#define AOS_DLFCN_H

#ifdef __cplusplus
extern "C" {
#endif

#define RTLD_LAZY   0x0001
#define RTLD_NOW    0x0002
#define RTLD_LOCAL  0x0000
#define RTLD_GLOBAL 0x0100

typedef struct {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
} Dl_info;

/** @brief Always fails. @return NULL; dlerror() then reports the reason. */
void *dlopen(const char *file, int mode);

/** @brief Always fails. @return NULL; dlerror() then reports the reason. */
void *dlsym(void *handle, const char *name);

/** @brief Nothing to close. @return -1; dlerror() then reports the reason. */
int dlclose(void *handle);

/**
 * @brief Describes the last dl* failure in the calling thread.
 * @return A static message, or NULL when no call failed since the last
 *         dlerror() in this thread.
 */
char *dlerror(void);

/** @brief No symbol information is available. @return 0. */
int dladdr(const void *addr, Dl_info *info);

#ifdef __cplusplus
}
#endif

#endif /* AOS_DLFCN_H */
