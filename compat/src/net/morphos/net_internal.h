/*
 * Private header of libaosnet for MorphOS.
 *
 * NetStack keeps socket descriptors per task: a descriptor of one thread
 * means nothing, or another socket, in the next. libaosnet hands out socket
 * numbers of its own, from AOS_SOCK_BASE up, above the file descriptors of
 * libnix, and gives each thread its own copy of a socket the first time the
 * thread uses it (ReleaseCopyOfSocket()/ObtainSocket(), net_fd.c).
 *
 * The NetStack calls themselves are in net_ns.c, the only file that sees
 * the inline calls of <proto/socket.h>, whose macros have the names of the
 * POSIX functions libaosnet defines.
 */
#ifndef AOS_NET_MORPHOS_INTERNAL_H
#define AOS_NET_MORPHOS_INTERNAL_H

/* aos_compat.h is forced in by the build. */
#ifndef AOS_COMPAT_H
#error "libaosnet must be compiled with -include aos_compat.h"
#endif

#include <errno.h>
#include <netdb.h>
#include <sys/time.h>

/* Socket numbers of libaosnet: AOS_SOCK_BASE to AOS_SOCK_BASE + AOS_SOCK_MAX
   - 1, below FD_SETSIZE (compat_morphos.h). NetStack allows 256 descriptors
   per task. */
#define AOS_SOCK_BASE 512
#define AOS_SOCK_MAX  256

/* ---- NetStack calls on descriptors of the calling task (net_ns.c) ----
   Each returns what the library returns; when that is a failure, errno
   holds Errno(), which NetStack keeps per task. */

int aos_ns_socket(int domain, int type, int protocol);
int aos_ns_close(int fd);
int aos_ns_bind(int fd, const struct sockaddr *addr, socklen_t len);
int aos_ns_connect(int fd, const struct sockaddr *addr, socklen_t len);
int aos_ns_listen(int fd, int backlog);
int aos_ns_accept(int fd, struct sockaddr *addr, socklen_t *len);
int aos_ns_shutdown(int fd, int how);
ssize_t aos_ns_send(int fd, const void *buf, size_t len, int flags);
ssize_t aos_ns_recv(int fd, void *buf, size_t len, int flags);
ssize_t aos_ns_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *to,
                      socklen_t tolen);
ssize_t aos_ns_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen);
ssize_t aos_ns_sendmsg(int fd, const struct msghdr *msg, int flags);
ssize_t aos_ns_recvmsg(int fd, struct msghdr *msg, int flags);
int aos_ns_getsockopt(int fd, int level, int name, void *val, socklen_t *len);
int aos_ns_setsockopt(int fd, int level, int name, const void *val, socklen_t len);
int aos_ns_getsockname(int fd, struct sockaddr *addr, socklen_t *len);
int aos_ns_getpeername(int fd, struct sockaddr *addr, socklen_t *len);
int aos_ns_ioctl(int fd, unsigned long request, void *arg);
/* WaitSelect() on descriptors of the calling task; mask may be NULL. */
int aos_ns_wait(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *timeout, unsigned long *mask);
/* Copy of a socket for another task: the id ObtainSocket() takes, or -1. */
long aos_ns_release_copy(int fd);
int aos_ns_obtain(long id, int domain, int type, int protocol);
int aos_ns_gethostname(char *name, size_t len);

/* The database calls return data in the library base, which all threads
   share: call them between aos_ns_db_lock() and aos_ns_db_unlock() and copy
   what they return before unlocking. h_errno of a failed host lookup is
   returned through *herr. */
void aos_ns_db_lock(void);
void aos_ns_db_unlock(void);
struct hostent *aos_ns_gethostbyname(const char *name, int *herr);
struct hostent *aos_ns_gethostbyaddr(const void *addr, int len, int type, int *herr);
struct servent *aos_ns_getservbyname(const char *name, const char *proto);
struct servent *aos_ns_getservbyport(int port, const char *proto);

/* Whether bsdsocket.library is open (libauto opens it before main()). */
int aos_ns_available(void);

/* ---- Socket numbers (net_fd.c) ---- */

/* Whether fd is in the range of libaosnet socket numbers. */
int aos_sock_in_range(int fd);
/* The calling thread's descriptor for socket number fd, taking a copy when
   the thread has none yet; -1 with errno EBADF for a number that is not an
   open socket. */
int aos_sock_get(int fd);
/* Registers nsfd, a new descriptor of the calling thread, and returns its
   socket number; on failure closes nsfd and returns -1 with errno set. */
int aos_sock_add(int nsfd, int domain, int type, int protocol);
/* Closes socket number fd for every thread (POSIX close()). */
int aos_sock_close(int fd);
/* O_NONBLOCK of socket number fd: set through FIONBIO, which applies to all
   copies, and remembered for F_GETFL. */
int aos_sock_set_nonblock(int fd, int on);
int aos_sock_nonblock(int fd);

#endif /* AOS_NET_MORPHOS_INTERNAL_H */
