/*
 * The NetStack calls of libaosnet, one thin function per call.
 *
 * The inline calls of <proto/socket.h> are macros named like the POSIX
 * functions libaosnet defines, so only this file includes it, without the
 * prototypes of <clib/socket_protos.h>, whose LONG types clash with the
 * POSIX declarations of aos_compat.h. All threads call through the
 * SocketBase that libauto opens before main(); NetStack keeps descriptors
 * and Errno() per task, so each call works on the calling thread's own
 * descriptors.
 */
#include "net_internal.h"

#include <exec/types.h>
#include <pthread.h>
#include <utility/tagitem.h>

#define CLIB_SOCKET_PROTOS_H 1
#undef PROTO_SOCKET_H
#include <proto/socket.h>

/* NetStack takes lengths as LONG; longer transfers become partial ones. */
#define AOS_NS_LONG_MAX 0x7fffffffUL

static LONG aos_ns_len(size_t len) {
    return len > AOS_NS_LONG_MAX ? (LONG)AOS_NS_LONG_MAX : (LONG)len;
}

/* errno from NetStack after a failed call: libnix does not always set
   errno for a call made in a thread, Errno() is always right. */
static long aos_ns_result(long rc) {
    if (rc < 0) {
        errno = (int)Errno();
    }
    return rc;
}

int aos_ns_available(void) {
    return SocketBase != NULL;
}

int aos_ns_socket(int domain, int type, int protocol) {
    return (int)aos_ns_result(socket(domain, type, protocol));
}

int aos_ns_close(int fd) {
    return (int)aos_ns_result(CloseSocket(fd));
}

int aos_ns_bind(int fd, const struct sockaddr *addr, socklen_t len) {
    return (int)aos_ns_result(bind(fd, addr, aos_ns_len(len)));
}

int aos_ns_connect(int fd, const struct sockaddr *addr, socklen_t len) {
    return (int)aos_ns_result(connect(fd, addr, aos_ns_len(len)));
}

int aos_ns_listen(int fd, int backlog) {
    return (int)aos_ns_result(listen(fd, backlog));
}

int aos_ns_accept(int fd, struct sockaddr *addr, socklen_t *len) {
    LONG l = len != NULL ? aos_ns_len(*len) : 0;
    long rc = aos_ns_result(accept(fd, addr, len != NULL ? &l : NULL));
    if (rc >= 0 && len != NULL) {
        *len = (socklen_t)l;
    }
    return (int)rc;
}

int aos_ns_shutdown(int fd, int how) {
    return (int)aos_ns_result(shutdown(fd, how));
}

ssize_t aos_ns_send(int fd, const void *buf, size_t len, int flags) {
    return aos_ns_result(send(fd, (const UBYTE *)buf, aos_ns_len(len), flags));
}

ssize_t aos_ns_recv(int fd, void *buf, size_t len, int flags) {
    return aos_ns_result(recv(fd, (UBYTE *)buf, aos_ns_len(len), flags));
}

ssize_t aos_ns_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *to,
                      socklen_t tolen) {
    return aos_ns_result(sendto(fd, (const UBYTE *)buf, aos_ns_len(len), flags, to, aos_ns_len(tolen)));
}

ssize_t aos_ns_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen) {
    LONG l = fromlen != NULL ? aos_ns_len(*fromlen) : 0;
    long rc = aos_ns_result(recvfrom(fd, (UBYTE *)buf, aos_ns_len(len), flags, from, fromlen != NULL ? &l : NULL));
    if (rc >= 0 && fromlen != NULL) {
        *fromlen = (socklen_t)l;
    }
    return rc;
}

ssize_t aos_ns_sendmsg(int fd, const struct msghdr *msg, int flags) {
    return aos_ns_result(sendmsg(fd, (struct msghdr *)msg, flags));
}

ssize_t aos_ns_recvmsg(int fd, struct msghdr *msg, int flags) {
    return aos_ns_result(recvmsg(fd, msg, flags));
}

int aos_ns_getsockopt(int fd, int level, int name, void *val, socklen_t *len) {
    LONG l = len != NULL ? aos_ns_len(*len) : 0;
    long rc = aos_ns_result(getsockopt(fd, level, name, val, len != NULL ? &l : NULL));
    if (rc >= 0 && len != NULL) {
        *len = (socklen_t)l;
    }
    return (int)rc;
}

int aos_ns_setsockopt(int fd, int level, int name, const void *val, socklen_t len) {
    return (int)aos_ns_result(setsockopt(fd, level, name, (APTR)val, aos_ns_len(len)));
}

int aos_ns_getsockname(int fd, struct sockaddr *addr, socklen_t *len) {
    LONG l = len != NULL ? aos_ns_len(*len) : 0;
    long rc = aos_ns_result(getsockname(fd, addr, len != NULL ? &l : NULL));
    if (rc >= 0 && len != NULL) {
        *len = (socklen_t)l;
    }
    return (int)rc;
}

int aos_ns_getpeername(int fd, struct sockaddr *addr, socklen_t *len) {
    LONG l = len != NULL ? aos_ns_len(*len) : 0;
    long rc = aos_ns_result(getpeername(fd, addr, len != NULL ? &l : NULL));
    if (rc >= 0 && len != NULL) {
        *len = (socklen_t)l;
    }
    return (int)rc;
}

int aos_ns_ioctl(int fd, unsigned long request, void *arg) {
    return (int)aos_ns_result(IoctlSocket(fd, request, (char *)arg));
}

int aos_ns_wait(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *timeout, unsigned long *mask) {
    ULONG m = mask != NULL ? (ULONG)*mask : 0;
    long rc = aos_ns_result(WaitSelect(nfds, r, w, e, timeout, mask != NULL ? &m : NULL));
    if (mask != NULL) {
        *mask = m;
    }
    return (int)rc;
}

long aos_ns_release_copy(int fd) {
    /* UNIQUE_ID of the bsdsocket autodoc, missing in the SDK headers
       (context_MorphOS/004): NetStack generates the id. */
    return aos_ns_result(ReleaseCopyOfSocket(fd, -1));
}

int aos_ns_obtain(long id, int domain, int type, int protocol) {
    return (int)aos_ns_result(ObtainSocket(id, domain, type, protocol));
}

int aos_ns_gethostname(char *name, size_t len) {
    return (int)aos_ns_result(gethostname((STRPTR)name, aos_ns_len(len)));
}

static pthread_mutex_t aos_ns_db_mutex = PTHREAD_MUTEX_INITIALIZER;

void aos_ns_db_lock(void) {
    pthread_mutex_lock(&aos_ns_db_mutex);
}

void aos_ns_db_unlock(void) {
    pthread_mutex_unlock(&aos_ns_db_mutex);
}

struct hostent *aos_ns_gethostbyname(const char *name, int *herr) {
    struct hostent *h = gethostbyname((const UBYTE *)name);
    *herr = h == NULL ? h_errno : 0;
    return h;
}

struct hostent *aos_ns_gethostbyaddr(const void *addr, int len, int type, int *herr) {
    struct hostent *h = gethostbyaddr((const UBYTE *)addr, len, type);
    *herr = h == NULL ? h_errno : 0;
    return h;
}

struct servent *aos_ns_getservbyname(const char *name, const char *proto) {
    return getservbyname((const UBYTE *)name, (const UBYTE *)proto);
}

struct servent *aos_ns_getservbyport(int port, const char *proto) {
    return getservbyport(port, (const UBYTE *)proto);
}
