/* dlopen() family for a system without run-time ELF loading. */
#include <dlfcn.h>

#include <stddef.h>

static char aos_dl_unsupported[] = "dynamic loading is not supported on AROS";

/* Per thread, as POSIX requires for dlerror(). */
static __thread char *aos_dl_error;

void *dlopen(const char *file, int mode)
{
    (void)file;
    (void)mode;
    aos_dl_error = aos_dl_unsupported;
    return NULL;
}

void *dlsym(void *handle, const char *name)
{
    (void)handle;
    (void)name;
    aos_dl_error = aos_dl_unsupported;
    return NULL;
}

int dlclose(void *handle)
{
    (void)handle;
    aos_dl_error = aos_dl_unsupported;
    return -1;
}

char *dlerror(void)
{
    char *error = aos_dl_error;
    aos_dl_error = NULL;
    return error;
}

int dladdr(const void *addr, Dl_info *info)
{
    (void)addr;
    (void)info;
    return 0;
}
