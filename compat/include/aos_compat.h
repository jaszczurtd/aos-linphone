/*
 * aos_compat.h - POSIX view of the AROS network API.
 *
 * Every translation unit that calls socket functions through libaosnet,
 * including the whole linphone stack, is compiled with this header forced
 * in first:
 *
 *     -include aos_compat.h
 *
 * The AROS SDK declares the bsdsocket.library calls in
 * <clib/bsdsocket_protos.h> without extern "C", with AROS types (an int
 * length for sendto() and friends) and with unconditional select() and
 * inet_ntoa() macros, and <proto/bsdsocket.h> turns every call into a
 * library-vector macro. This header blocks both and declares the calls as
 * plain POSIX functions with C linkage; libaosnet implements them. Types and
 * constants still come from the AROS headers, so structure layouts match on
 * both sides.
 *
 * The header holds declarations only, so that code compiled against it does
 * not depend on how libaosnet implements them.
 */
#ifndef AOS_COMPAT_H
#define AOS_COMPAT_H

/* CMake's check_function_exists() and check_library_exists() compile
   "char f(void); ... f();" with -DCHECK_FUNCTION_EXISTS=f and only test
   the link. The declarations below (and <pthread.h>) would conflict with
   that, and such a check would report a present function as missing. */
#ifndef CHECK_FUNCTION_EXISTS

#if !defined(__AROS__)
#error "aos_compat.h is for AROS targets only"
#endif

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
#define _XOPEN_SOURCE 700
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
#define BSDSOCKET_NOLIBDEFINES 1
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

struct addrinfo;
struct Library;
struct TagItem;

/* Opened by libaosnet at program start and shared by all threads. */
extern struct Library *SocketBase;

int socket(int domain, int type, int protocol);
int bind(int s, const struct sockaddr *addr, socklen_t addrlen);
int connect(int s, const struct sockaddr *addr, socklen_t addrlen);
int listen(int s, int backlog);
int accept(int s, struct sockaddr *addr, socklen_t *addrlen);
int shutdown(int s, int how);
ssize_t send(int s, const void *buf, size_t len, int flags);
ssize_t recv(int s, void *buf, size_t len, int flags);
ssize_t sendto(int s, const void *buf, size_t len, int flags,
               const struct sockaddr *to, socklen_t tolen);
ssize_t recvfrom(int s, void *buf, size_t len, int flags,
                 struct sockaddr *from, socklen_t *fromlen);
ssize_t sendmsg(int s, const struct msghdr *msg, int flags);
ssize_t recvmsg(int s, struct msghdr *msg, int flags);
int getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen);
int setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen);
int getsockname(int s, struct sockaddr *addr, socklen_t *addrlen);
int getpeername(int s, struct sockaddr *addr, socklen_t *addrlen);
int gethostname(char *name, size_t namelen);

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout);

in_addr_t inet_addr(const char *cp);
int inet_aton(const char *cp, struct in_addr *addr);
char *inet_ntoa(struct in_addr in);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
int inet_pton(int af, const char *src, void *dst);

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags);
const char *gai_strerror(int errcode);

/* The h_errno helper in <netdb.h> calls this through SocketBase. */
ULONG SocketBaseTagList(struct TagItem *tags);

/* GNU extension from libaoscompat: the current directory in a buffer from
   malloc(). liblinphone calls it when $HOME is not set. */
char *get_current_dir_name(void);

/* BSD function from libaoscompat, missing in posixc: the page size from
   sysconf(_SC_PAGESIZE). mediastreamer2 uses it for aligned buffers. */
int getpagesize(void);

/* XSI function that posixc implements but declares only under _GNU_SOURCE.
   ortp seeds random() with it. */
void srandom(unsigned);

#ifdef __cplusplus
}
#endif

#endif /* !CHECK_FUNCTION_EXISTS */

#endif /* AOS_COMPAT_H */
