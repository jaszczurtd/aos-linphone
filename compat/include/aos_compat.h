/*
 * aos_compat.h - POSIX view of the AROS and MorphOS network APIs.
 *
 * Every translation unit that calls socket functions through libaosnet,
 * including the whole linphone stack, is compiled with this header forced
 * in first:
 *
 *     -include aos_compat.h
 *
 * The system SDKs declare the bsdsocket.library calls their own way: AROS
 * in <clib/bsdsocket_protos.h> without extern "C", with AROS types and with
 * select() and inet_ntoa() macros, MorphOS libnix as inline calls on a
 * global SocketBase in <proto/socket.h>. The system part of this header
 * (aos/compat_aros.h, aos/compat_morphos.h) blocks them, fixes the feature
 * environment and adds what the system headers lack; the common part
 * declares the calls as plain POSIX functions with C linkage, which
 * libaosnet implements. Types and constants come from the system headers
 * where they exist, so structure layouts match on both sides.
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

#if defined(__AROS__)
#include "aos/compat_aros.h"
#elif defined(__MORPHOS__)
#include "aos/compat_morphos.h"
#else
#error "aos_compat.h is for AROS and MorphOS targets only"
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct addrinfo;

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

/* GNU extension from libaoscompat: the current directory in a buffer from
   malloc(). liblinphone calls it when $HOME is not set. */
char *get_current_dir_name(void);

/* BSD function from libaoscompat, missing in posixc: the page size.
   mediastreamer2 uses it for aligned buffers. */
int getpagesize(void);

#ifdef __cplusplus
}
#endif

#endif /* !CHECK_FUNCTION_EXISTS */

#endif /* AOS_COMPAT_H */
