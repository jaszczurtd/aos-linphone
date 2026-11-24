/*
 * Private header of libaosnet. Gives the implementation the bsdsocket.library
 * vector calls (__socket_WB() and so on) without the macros that would
 * collide with the POSIX functions it defines.
 */
#ifndef AOS_NET_INTERNAL_H
#define AOS_NET_INTERNAL_H

/* aos_compat.h is forced in by the build. */
#ifndef AOS_COMPAT_H
#error "libaosnet must be compiled with -include aos_compat.h"
#endif

#include <errno.h>

/* Also brings the vector calls of the Roadshow API (getaddrinfo(), the DNS
   server list...), which AROSTCP implements. */
#define __CONFIG_ROADSHOW__ 1
#include <defines/bsdsocket.h>

/* Names of the POSIX functions defined by libaosnet that
   <defines/bsdsocket.h> turns into vector macros. */
#undef select
#undef socket
#undef bind
#undef connect
#undef listen
#undef accept
#undef shutdown
#undef send
#undef recv
#undef sendto
#undef recvfrom
#undef sendmsg
#undef recvmsg
#undef getsockopt
#undef setsockopt
#undef getsockname
#undef getpeername
#undef gethostname
#undef inet_addr
#undef inet_aton
#undef inet_ntop
#undef inet_pton
#undef getaddrinfo
#undef freeaddrinfo
#undef getnameinfo
#undef gai_strerror
#undef SocketBaseTagList

/* Returns from the calling function with -1 and errno set when
   bsdsocket.library could not be opened at program start. */
#define AOS_NET_REQUIRE_BASE(failure)          \
    do {                                       \
        if (SocketBase == NULL) {              \
            errno = aos_net_open_error;        \
            return (failure);                  \
        }                                      \
    } while (0)

/* errno value describing why SocketBase is NULL; 0 while it is open. */
extern int aos_net_open_error;

#endif /* AOS_NET_INTERNAL_H */
