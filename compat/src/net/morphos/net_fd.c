/*
 * Socket numbers of libaosnet and the copies of each socket per thread.
 *
 * NetStack descriptors belong to the task that made them, and they start
 * at 0 like the file descriptors of libnix. libaosnet therefore gives each
 * socket a number of its own (AOS_SOCK_BASE + slot) and keeps, for each
 * thread, the thread's NetStack descriptor of that socket:
 *
 *  - the thread that creates a socket releases a spare copy of it at once
 *    (ReleaseCopyOfSocket());
 *  - a thread that uses the socket for the first time takes the spare copy
 *    (ObtainSocket()) and releases a new one from its own descriptor, so a
 *    spare copy always exists;
 *  - close() shuts the socket down, which wakes WaitSelect() on every copy
 *    and ends a TCP connection, takes back and closes the spare copy, and
 *    closes the caller's copy. The copies of other threads close at their
 *    next call into libaosnet, or when the thread ends (NetStack closes the
 *    descriptors of an ended task). Until then a UDP port stays bound.
 *  - NetStack does not close released copies when the program ends, so an
 *    atexit() handler takes back and closes the spare copies of the sockets
 *    still open; without it their ports would stay bound until a reboot.
 *
 * Socket state (FIONBIO, the local address, the connection) is shared by
 * all copies of a socket.
 *
 * close(), read(), write() and fcntl() of libnix work on its file
 * descriptors only, and close() and fcntl() share an object file with
 * open(), so libaosnet cannot replace them by plain definitions. Programs
 * are linked with --wrap for these names (the aosnet target in
 * compat/CMakeLists.txt, sdk/morphos-sdk.cmake): the __wrap_ functions below handle socket numbers and pass any other
 * descriptor to libnix through __real_. libnix has no ioctl(); it is
 * defined here for sockets only.
 */
#include "net_internal.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

struct aos_sock {
    int open;
    unsigned int gen; /* changes when the socket is closed */
    int domain, type, protocol;
    int nonblock;
    long spare; /* id of the released copy, -1 when there is none */
};

/* What one thread holds: its descriptor for each slot and the generation of
   the socket it was taken from. */
struct aos_sock_map {
    int fd[AOS_SOCK_MAX];
    unsigned int gen[AOS_SOCK_MAX];
    unsigned int closes_seen;
};

static struct aos_sock aos_socks[AOS_SOCK_MAX];
static pthread_mutex_t aos_socks_lock = PTHREAD_MUTEX_INITIALIZER;
/* Counts close() calls, so a thread looks for stale copies only after one. */
static volatile unsigned int aos_socks_closes;

/* Released copies close() could not take back (the closing task had no
   free descriptor): taken back at a later close() or at exit. */
#define AOS_ORPHANS_MAX 16
static struct {
    long id;
    int domain, type, protocol;
} aos_orphans[AOS_ORPHANS_MAX];
static int aos_orphan_count;

static pthread_once_t aos_exit_once = PTHREAD_ONCE_INIT;

/* Takes back and closes the orphaned copies it can. Called with
   aos_socks_lock held. */
static void aos_orphans_reclaim(void) {
    int kept = 0;
    for (int i = 0; i < aos_orphan_count; ++i) {
        int fd = aos_ns_obtain(aos_orphans[i].id, aos_orphans[i].domain, aos_orphans[i].type,
                               aos_orphans[i].protocol);
        if (fd >= 0) {
            aos_ns_close(fd);
        } else {
            aos_orphans[kept++] = aos_orphans[i];
        }
    }
    aos_orphan_count = kept;
}

/* At program exit, in the main task, before libauto closes
   bsdsocket.library: the spare copies of the sockets still open. Other
   copies belong to tasks NetStack cleans up itself. */
static void aos_socks_at_exit(void) {
    /* A thread that died holding the lock must not stop the exit. */
    int locked = pthread_mutex_trylock(&aos_socks_lock) == 0;
    for (int i = 0; i < AOS_SOCK_MAX; ++i) {
        struct aos_sock *s = &aos_socks[i];
        if (s->open && s->spare >= 0) {
            int fd = aos_ns_obtain(s->spare, s->domain, s->type, s->protocol);
            if (fd >= 0) {
                aos_ns_close(fd);
            }
            s->spare = -1;
        }
    }
    aos_orphans_reclaim();
    if (locked) {
        pthread_mutex_unlock(&aos_socks_lock);
    }
}

static void aos_exit_register(void) {
    atexit(aos_socks_at_exit);
}

static pthread_key_t aos_map_key;
static pthread_once_t aos_map_once = PTHREAD_ONCE_INIT;

/* At the end of a thread: close its copies (NetStack would close them too,
   once the task is gone) and free the map. */
static void aos_map_free(void *ptr) {
    struct aos_sock_map *map = ptr;
    for (int i = 0; i < AOS_SOCK_MAX; ++i) {
        if (map->fd[i] >= 0) {
            aos_ns_close(map->fd[i]);
        }
    }
    free(map);
}

static void aos_map_init_key(void) {
    pthread_key_create(&aos_map_key, aos_map_free);
}

/* The calling thread's map, created on first use; NULL without memory. */
static struct aos_sock_map *aos_map(void) {
    pthread_once(&aos_map_once, aos_map_init_key);
    struct aos_sock_map *map = pthread_getspecific(aos_map_key);
    if (map == NULL) {
        map = malloc(sizeof(*map));
        if (map == NULL) {
            return NULL;
        }
        for (int i = 0; i < AOS_SOCK_MAX; ++i) {
            map->fd[i] = -1;
            map->gen[i] = 0;
        }
        map->closes_seen = aos_socks_closes;
        if (pthread_setspecific(aos_map_key, map) != 0) {
            free(map);
            return NULL;
        }
    }
    return map;
}

/* Closes the thread's copies of sockets that were closed since its last
   look. Called with aos_socks_lock held. */
static void aos_map_sweep(struct aos_sock_map *map) {
    if (map->closes_seen == aos_socks_closes) {
        return;
    }
    for (int i = 0; i < AOS_SOCK_MAX; ++i) {
        if (map->fd[i] >= 0 && (!aos_socks[i].open || map->gen[i] != aos_socks[i].gen)) {
            int saved = errno;
            aos_ns_close(map->fd[i]);
            errno = saved;
            map->fd[i] = -1;
        }
    }
    map->closes_seen = aos_socks_closes;
}

int aos_sock_in_range(int fd) {
    return fd >= AOS_SOCK_BASE && fd < AOS_SOCK_BASE + AOS_SOCK_MAX;
}

int aos_sock_get(int fd) {
    if (!aos_sock_in_range(fd)) {
        errno = EBADF;
        return -1;
    }
    int slot = fd - AOS_SOCK_BASE;
    struct aos_sock_map *map = aos_map();
    if (map == NULL) {
        errno = ENOMEM;
        return -1;
    }

    pthread_mutex_lock(&aos_socks_lock);
    aos_map_sweep(map);
    struct aos_sock *s = &aos_socks[slot];
    int nsfd = -1;
    if (!s->open) {
        errno = EBADF;
    } else if (map->fd[slot] >= 0) {
        nsfd = map->fd[slot];
        if (s->spare < 0) {
            /* Releasing the spare copy failed before: try again from this
               thread's copy, so that other threads can take one. */
            s->spare = aos_ns_release_copy(nsfd);
        }
    } else if (s->spare < 0) {
        /* Releasing the last spare copy failed. */
        errno = EBADF;
    } else {
        nsfd = aos_ns_obtain(s->spare, s->domain, s->type, s->protocol);
        if (nsfd >= 0) {
            s->spare = aos_ns_release_copy(nsfd);
            map->fd[slot] = nsfd;
            map->gen[slot] = s->gen;
        }
    }
    pthread_mutex_unlock(&aos_socks_lock);
    return nsfd;
}

int aos_sock_add(int nsfd, int domain, int type, int protocol) {
    pthread_once(&aos_exit_once, aos_exit_register);
    struct aos_sock_map *map = aos_map();
    if (map == NULL) {
        aos_ns_close(nsfd);
        errno = ENOMEM;
        return -1;
    }

    pthread_mutex_lock(&aos_socks_lock);
    aos_map_sweep(map);
    int slot = -1;
    for (int i = 0; i < AOS_SOCK_MAX; ++i) {
        /* A slot the thread still holds a copy of stays out of use until
           that copy is gone. */
        if (!aos_socks[i].open && map->fd[i] < 0) {
            slot = i;
            break;
        }
    }
    long spare = slot >= 0 ? aos_ns_release_copy(nsfd) : -1;
    if (slot < 0 || spare < 0) {
        int err = slot < 0 ? EMFILE : errno;
        pthread_mutex_unlock(&aos_socks_lock);
        aos_ns_close(nsfd);
        errno = err;
        return -1;
    }
    struct aos_sock *s = &aos_socks[slot];
    s->open = 1;
    s->domain = domain;
    s->type = type;
    s->protocol = protocol;
    s->nonblock = 0;
    s->spare = spare;
    map->fd[slot] = nsfd;
    map->gen[slot] = s->gen;
    pthread_mutex_unlock(&aos_socks_lock);
    return AOS_SOCK_BASE + slot;
}

int aos_sock_close(int fd) {
    if (!aos_sock_in_range(fd)) {
        errno = EBADF;
        return -1;
    }
    int slot = fd - AOS_SOCK_BASE;
    struct aos_sock_map *map = aos_map();

    pthread_mutex_lock(&aos_socks_lock);
    struct aos_sock *s = &aos_socks[slot];
    if (!s->open) {
        pthread_mutex_unlock(&aos_socks_lock);
        errno = EBADF;
        return -1;
    }
    int own = map != NULL && map->fd[slot] >= 0 && map->gen[slot] == s->gen ? map->fd[slot] : -1;
    int spare = s->spare >= 0 ? aos_ns_obtain(s->spare, s->domain, s->type, s->protocol) : -1;
    if (s->spare >= 0 && spare < 0 && aos_orphan_count < AOS_ORPHANS_MAX) {
        /* No free descriptor in this task now: take the copy back later. */
        aos_orphans[aos_orphan_count].id = s->spare;
        aos_orphans[aos_orphan_count].domain = s->domain;
        aos_orphans[aos_orphan_count].type = s->type;
        aos_orphans[aos_orphan_count].protocol = s->protocol;
        ++aos_orphan_count;
    }
    /* Through any descriptor of this thread: the shutdown reaches every
       copy and wakes WaitSelect() on them, also for a UDP socket that is
       bound but not connected (morphos-vm/tests/copyprobe.c). */
    int via = own >= 0 ? own : spare;
    if (via >= 0) {
        aos_ns_shutdown(via, SHUT_RDWR);
    }
    if (spare >= 0) {
        aos_ns_close(spare);
    }
    if (own >= 0) {
        aos_ns_close(own);
        map->fd[slot] = -1;
    }
    if (aos_orphan_count > 0) {
        aos_orphans_reclaim();
    }
    s->open = 0;
    s->spare = -1;
    ++s->gen;
    ++aos_socks_closes;
    if (map != NULL) {
        aos_map_sweep(map);
    }
    pthread_mutex_unlock(&aos_socks_lock);
    return 0;
}

int aos_sock_set_nonblock(int fd, int on) {
    int nsfd = aos_sock_get(fd);
    if (nsfd < 0) {
        return -1;
    }
    long value = on ? 1 : 0;
    if (aos_ns_ioctl(nsfd, FIONBIO, &value) != 0) {
        return -1;
    }
    pthread_mutex_lock(&aos_socks_lock);
    aos_socks[fd - AOS_SOCK_BASE].nonblock = on ? 1 : 0;
    pthread_mutex_unlock(&aos_socks_lock);
    return 0;
}

int aos_sock_nonblock(int fd) {
    if (!aos_sock_in_range(fd)) {
        return 0;
    }
    pthread_mutex_lock(&aos_socks_lock);
    int on = aos_socks[fd - AOS_SOCK_BASE].nonblock;
    pthread_mutex_unlock(&aos_socks_lock);
    return on;
}

/* ---- libnix functions extended to socket numbers (--wrap) ---- */

int __real_close(int fd);
ssize_t __real_read(int fd, void *buf, size_t len);
ssize_t __real_write(int fd, const void *buf, size_t len);
int __real_fcntl(int fd, int cmd, ...);

int __wrap_close(int fd);
ssize_t __wrap_read(int fd, void *buf, size_t len);
ssize_t __wrap_write(int fd, const void *buf, size_t len);
int __wrap_fcntl(int fd, int cmd, ...);

int __wrap_close(int fd) {
    return aos_sock_in_range(fd) ? aos_sock_close(fd) : __real_close(fd);
}

ssize_t __wrap_read(int fd, void *buf, size_t len) {
    return aos_sock_in_range(fd) ? recv(fd, buf, len, 0) : __real_read(fd, buf, len);
}

ssize_t __wrap_write(int fd, const void *buf, size_t len) {
    return aos_sock_in_range(fd) ? send(fd, buf, len, 0) : __real_write(fd, buf, len);
}

int __wrap_fcntl(int fd, int cmd, ...) {
    va_list ap;
    va_start(ap, cmd);
    long arg = va_arg(ap, long);
    va_end(ap);

    if (!aos_sock_in_range(fd)) {
        return __real_fcntl(fd, cmd, arg);
    }
    if (aos_sock_get(fd) < 0) {
        return -1;
    }
    switch (cmd) {
    case F_GETFL:
        return O_RDWR | (aos_sock_nonblock(fd) ? O_NONBLOCK : 0);
    case F_SETFL:
        return aos_sock_set_nonblock(fd, (arg & O_NONBLOCK) != 0);
    case F_GETFD:
    case F_SETFD:
        /* No exec() on MorphOS: close-on-exec has no meaning. */
        return 0;
    default:
        errno = EINVAL;
        return -1;
    }
}

int ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    int nsfd = aos_sock_get(fd);
    if (nsfd < 0) {
        /* Not a socket: libnix has no ioctl() for its files. */
        if (!aos_sock_in_range(fd)) {
            errno = ENOTTY;
        }
        return -1;
    }
    if (request == FIONBIO) {
        return aos_sock_set_nonblock(fd, arg != NULL && *(const int *)arg != 0);
    }
    return aos_ns_ioctl(nsfd, request, arg);
}
