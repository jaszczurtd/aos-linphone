/*
 * POSIX socket calls over the shared bsdsocket.library base.
 *
 * close(), read(), write(), fcntl() and ioctl() on sockets are not here:
 * posixc passes them to AROSTCP through fd.library.
 */
#include "net_internal.h"

#include <limits.h>

/* bsdsocket.library takes buffer lengths as int. A longer stream transfer
   becomes a partial one, which POSIX allows; a datagram that long cannot be
   sent anyway. */
static int aos_net_len(size_t len)
{
    return len > (size_t)INT_MAX ? INT_MAX : (int)len;
}

int socket(int domain, int type, int protocol)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __socket_WB(SocketBase, domain, type, protocol);
}

int bind(int s, const struct sockaddr *addr, socklen_t addrlen)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __bind_WB(SocketBase, s, addr, addrlen);
}

int connect(int s, const struct sockaddr *addr, socklen_t addrlen)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __connect_WB(SocketBase, s, addr, addrlen);
}

int listen(int s, int backlog)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __listen_WB(SocketBase, s, backlog);
}

int accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __accept_WB(SocketBase, s, addr, addrlen);
}

int shutdown(int s, int how)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __shutdown_WB(SocketBase, s, how);
}

ssize_t send(int s, const void *buf, size_t len, int flags)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __send_WB(SocketBase, s, buf, aos_net_len(len), flags);
}

ssize_t recv(int s, void *buf, size_t len, int flags)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __recv_WB(SocketBase, s, buf, aos_net_len(len), flags);
}

ssize_t sendto(int s, const void *buf, size_t len, int flags,
               const struct sockaddr *to, socklen_t tolen)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __sendto_WB(SocketBase, s, buf, aos_net_len(len), flags, to, tolen);
}

ssize_t recvfrom(int s, void *buf, size_t len, int flags,
                 struct sockaddr *from, socklen_t *fromlen)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __recvfrom_WB(SocketBase, s, buf, aos_net_len(len), flags, from, fromlen);
}

ssize_t sendmsg(int s, const struct msghdr *msg, int flags)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __sendmsg_WB(SocketBase, s, msg, flags);
}

ssize_t recvmsg(int s, struct msghdr *msg, int flags)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __recvmsg_WB(SocketBase, s, msg, flags);
}

int getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __getsockopt_WB(SocketBase, s, level, optname, optval, optlen);
}

int setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __setsockopt_WB(SocketBase, s, level, optname, optval, optlen);
}

int getsockname(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __getsockname_WB(SocketBase, s, addr, addrlen);
}

int getpeername(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __getpeername_WB(SocketBase, s, addr, addrlen);
}

int gethostname(char *name, size_t namelen)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __gethostname_WB(SocketBase, name, namelen);
}
