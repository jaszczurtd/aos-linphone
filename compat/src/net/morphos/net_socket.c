/*
 * POSIX socket calls on libaosnet socket numbers: each finds the calling
 * thread's NetStack descriptor of the socket (net_fd.c) and calls NetStack.
 *
 * NetStack has no IPv6: socket(AF_INET6) fails with EAFNOSUPPORT. It also
 * ignores MSG_DONTWAIT (context_MorphOS/003); for a blocking socket the
 * flag is done here by checking readiness with a zero-timeout WaitSelect()
 * first.
 */
#include "net_internal.h"

#include <string.h>
#include <sys/ioctl.h>

/* With MSG_DONTWAIT on a blocking socket: 0 when the call would not block,
   otherwise -1 with EWOULDBLOCK. */
static int aos_sock_dontwait(int fd, int nsfd, int flags, int writing) {
    if (!(flags & MSG_DONTWAIT) || aos_sock_nonblock(fd)) {
        return 0;
    }
    fd_set set;
    struct timeval now = {0, 0};
    FD_ZERO(&set);
    FD_SET(nsfd, &set);
    int rc = aos_ns_wait(nsfd + 1, writing ? NULL : &set, writing ? &set : NULL, NULL, &now, NULL);
    if (rc < 0) {
        return -1;
    }
    if (rc == 0) {
        errno = EWOULDBLOCK;
        return -1;
    }
    return 0;
}

int socket(int domain, int type, int protocol) {
    if (!aos_ns_available()) {
        errno = ENETDOWN;
        return -1;
    }
    if (domain != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    int nsfd = aos_ns_socket(domain, type, protocol);
    if (nsfd < 0) {
        return -1;
    }
    return aos_sock_add(nsfd, domain, type, protocol);
}

int bind(int s, const struct sockaddr *addr, socklen_t addrlen) {
    int nsfd = aos_sock_get(s);
    return nsfd < 0 ? -1 : aos_ns_bind(nsfd, addr, addrlen);
}

int connect(int s, const struct sockaddr *addr, socklen_t addrlen) {
    int nsfd = aos_sock_get(s);
    return nsfd < 0 ? -1 : aos_ns_connect(nsfd, addr, addrlen);
}

int listen(int s, int backlog) {
    int nsfd = aos_sock_get(s);
    return nsfd < 0 ? -1 : aos_ns_listen(nsfd, backlog);
}

int accept(int s, struct sockaddr *addr, socklen_t *addrlen) {
    int nsfd = aos_sock_get(s);
    if (nsfd < 0) {
        return -1;
    }
    int type = SOCK_STREAM, protocol = 0;
    socklen_t len = sizeof(type);
    aos_ns_getsockopt(nsfd, SOL_SOCKET, SO_TYPE, &type, &len);
    int conn = aos_ns_accept(nsfd, addr, addrlen);
    if (conn < 0) {
        return -1;
    }
    /* As on Linux, the new socket blocks whatever the listening one does. */
    long blocking = 0;
    aos_ns_ioctl(conn, FIONBIO, &blocking);
    return aos_sock_add(conn, AF_INET, type, protocol);
}

int shutdown(int s, int how) {
    int nsfd = aos_sock_get(s);
    return nsfd < 0 ? -1 : aos_ns_shutdown(nsfd, how);
}

ssize_t send(int s, const void *buf, size_t len, int flags) {
    int nsfd = aos_sock_get(s);
    if (nsfd < 0 || aos_sock_dontwait(s, nsfd, flags, 1) != 0) {
        return -1;
    }
    return aos_ns_send(nsfd, buf, len, flags & ~MSG_DONTWAIT);
}

ssize_t recv(int s, void *buf, size_t len, int flags) {
    int nsfd = aos_sock_get(s);
    if (nsfd < 0 || aos_sock_dontwait(s, nsfd, flags, 0) != 0) {
        return -1;
    }
    return aos_ns_recv(nsfd, buf, len, flags & ~MSG_DONTWAIT);
}

ssize_t sendto(int s, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) {
    int nsfd = aos_sock_get(s);
    if (nsfd < 0 || aos_sock_dontwait(s, nsfd, flags, 1) != 0) {
        return -1;
    }
    return aos_ns_sendto(nsfd, buf, len, flags & ~MSG_DONTWAIT, to, tolen);
}

ssize_t recvfrom(int s, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen) {
    int nsfd = aos_sock_get(s);
    if (nsfd < 0 || aos_sock_dontwait(s, nsfd, flags, 0) != 0) {
        return -1;
    }
    return aos_ns_recvfrom(nsfd, buf, len, flags & ~MSG_DONTWAIT, from, fromlen);
}

ssize_t sendmsg(int s, const struct msghdr *msg, int flags) {
    int nsfd = aos_sock_get(s);
    if (nsfd < 0 || aos_sock_dontwait(s, nsfd, flags, 1) != 0) {
        return -1;
    }
    return aos_ns_sendmsg(nsfd, msg, flags & ~MSG_DONTWAIT);
}

ssize_t recvmsg(int s, struct msghdr *msg, int flags) {
    int nsfd = aos_sock_get(s);
    if (nsfd < 0 || aos_sock_dontwait(s, nsfd, flags, 0) != 0) {
        return -1;
    }
    return aos_ns_recvmsg(nsfd, msg, flags & ~MSG_DONTWAIT);
}

int getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen) {
    int nsfd = aos_sock_get(s);
    return nsfd < 0 ? -1 : aos_ns_getsockopt(nsfd, level, optname, optval, optlen);
}

int setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen) {
    int nsfd = aos_sock_get(s);
    return nsfd < 0 ? -1 : aos_ns_setsockopt(nsfd, level, optname, optval, optlen);
}

int getsockname(int s, struct sockaddr *addr, socklen_t *addrlen) {
    int nsfd = aos_sock_get(s);
    return nsfd < 0 ? -1 : aos_ns_getsockname(nsfd, addr, addrlen);
}

int getpeername(int s, struct sockaddr *addr, socklen_t *addrlen) {
    int nsfd = aos_sock_get(s);
    return nsfd < 0 ? -1 : aos_ns_getpeername(nsfd, addr, addrlen);
}

int gethostname(char *name, size_t namelen) {
    if (!aos_ns_available()) {
        errno = ENETDOWN;
        return -1;
    }
    return aos_ns_gethostname(name, namelen);
}
