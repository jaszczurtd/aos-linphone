/*
 * POSIX functions that libnix declares but does not implement, as far as
 * the linphone stack needs them.
 *
 *  - setenv() sets a local DOS variable of the calling process, which
 *    getenv() reads first. Other threads (processes of their own on
 *    MorphOS) do not see it.
 *  - realpath() resolves through a DOS lock, so the object must exist.
 *  - srandom() seeds rand(), the generator libnix has.
 *  - fsync() has nothing to do: DOS has no call to flush one file to disk.
 *  - popen() and pclose() fail: the stack only uses them for optional
 *    helper commands.
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dos/var.h>
#include <proto/dos.h>

#define AOS_PATH_MAX 1024

int setenv(const char *name, const char *value, int overwrite) {
    if (name == NULL || name[0] == '\0' || strchr(name, '=') != NULL || value == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!overwrite && getenv(name) != NULL) {
        return 0;
    }
    if (!SetVar((CONST_STRPTR)name, (CONST_STRPTR)value, -1, GVF_LOCAL_ONLY)) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

char *realpath(const char *path, char *resolved) {
    if (path == NULL) {
        errno = EINVAL;
        return NULL;
    }
    BPTR lock = Lock((CONST_STRPTR)path, SHARED_LOCK);
    if (lock == 0) {
        errno = ENOENT;
        return NULL;
    }
    char *out = resolved != NULL ? resolved : malloc(AOS_PATH_MAX);
    if (out == NULL) {
        UnLock(lock);
        errno = ENOMEM;
        return NULL;
    }
    LONG ok = NameFromLock(lock, (STRPTR)out, AOS_PATH_MAX);
    UnLock(lock);
    if (!ok) {
        if (out != resolved) {
            free(out);
        }
        errno = ENAMETOOLONG;
        return NULL;
    }
    return out;
}

void srandom(unsigned seed) {
    srand(seed);
}

int fsync(int fd) {
    (void)fd;
    return 0;
}

FILE *popen(const char *command, const char *mode) {
    (void)command;
    (void)mode;
    errno = ENOSYS;
    return NULL;
}

int pclose(FILE *stream) {
    (void)stream;
    errno = ECHILD;
    return -1;
}
