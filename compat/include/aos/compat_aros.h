/*
 * AROS part of aos_compat.h: posixc, pthread and AROSTCP (included by
 * aos_compat.h only).
 */
#ifndef AOS_COMPAT_AROS_H
#define AOS_COMPAT_AROS_H

/* Feature visibility. When <sys/types.h> comes before <sys/cdefs.h>,
   aros/features.h falls back to _XOPEN_SOURCE 500 and <sys/cdefs.h> then
   hides the BSD part of the socket headers (SOCK_CLOEXEC, SO_NOSIGPIPE,
   MSG_DONTWAIT...), which decides code paths in the linphone stack. g++
   always defines _GNU_SOURCE (POSIX.1-2008, XSI 700, BSD); C units get the
   same environment here, without the GNU extensions, so that C and C++ see
   the same constants whatever the include order. Sources that define
   their own feature macros later get no say: the headers below fix the
   environment first. */
#if !defined(_POSIX_SOURCE) && !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) && \
    !defined(_GNU_SOURCE) && !defined(_ANSI_SOURCE) && !defined(_C99_SOURCE)
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE   700
#ifndef __BSD_VISIBLE
#define __BSD_VISIBLE 1
#endif
#endif

/* Descriptor numbers come from fd.library and are system-wide, so they can
   be larger than the posixc default of 64. Every unit must see the same
   value, which is why it is set here. */
#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif

/* <pthread.h> wraps socket calls in cancellation-point macros
   (#define send(...) (pthread_testcancel(), send(__VA_ARGS__))) when
   CLIB_BSDSOCKET_PROTOS_H is defined at the time it is first included. Such
   macros would also rewrite C++ methods named send(), connect(), accept()
   and so on. Including it here, before the guard below exists, keeps them
   out; the linphone stack does not use pthread_cancel(). */
#include <pthread.h>
#undef accept
#undef connect
#undef CloseSocket
#undef recv
#undef recvfrom
#undef recvmsg
#undef select
#undef send
#undef sendmsg
#undef sendto

/* No bsdsocket.library vector macros, no AROS prototypes. */
#define BSDSOCKET_NOLIBDEFINES  1
#define CLIB_BSDSOCKET_PROTOS_H 1

#include <exec/types.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* getaddrinfo() flags that AROSTCP lacks; libaosnet implements them. The
   values do not collide with the AI_* flags of the AROS <netdb.h>. */
#ifndef AI_ALL
#define AI_ALL 0x0100
#endif
#ifndef AI_V4MAPPED
#define AI_V4MAPPED 0x0800
#endif

/* Word views of an IPv6 address, as glibc and the BSDs name them. */
#ifndef s6_addr16
#define s6_addr16 un.u16_addr
#endif
#ifndef s6_addr32
#define s6_addr32 un.u32_addr
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct Library;
struct TagItem;

/* Opened by libaosnet at program start and shared by all threads. */
extern struct Library *SocketBase;

/* The h_errno helper in <netdb.h> calls this through SocketBase. */
ULONG SocketBaseTagList(struct TagItem *tags);

/* XSI function that posixc implements but declares only under _GNU_SOURCE.
   ortp seeds random() with it. */
void srandom(unsigned);

#ifdef __cplusplus
}
#endif

#endif /* AOS_COMPAT_AROS_H */
