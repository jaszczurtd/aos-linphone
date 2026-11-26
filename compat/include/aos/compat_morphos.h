/*
 * MorphOS part of aos_compat.h: libnix with libpthread and the NetStack
 * (included by aos_compat.h only).
 *
 * libnix turns the bsdsocket.library calls into inline functions on a
 * global SocketBase (<proto/socket.h>, which <sys/socket.h> includes), and
 * its headers predate much of the POSIX socket API. This part blocks the
 * inline calls and adds the missing types and constants. The NetStack has
 * no IPv6: its types are here so that code using them compiles, and
 * libaosnet rejects AF_INET6.
 */
#ifndef AOS_COMPAT_MORPHOS_H
#define AOS_COMPAT_MORPHOS_H

/* Every unit must see the same value; libnix defaults to 256. */
#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif

/* The PRI* and INT*_C macros of C99 headers in C++ too. */
#ifdef __cplusplus
#define __STDC_FORMAT_MACROS   1
#define __STDC_LIMIT_MACROS    1
#define __STDC_CONSTANT_MACROS 1
#endif

#include <pthread.h>
/* libnix's <pthread.h> defines it empty, which breaks "_POSIX_THREADS > 0". */
#undef _POSIX_THREADS
#define _POSIX_THREADS 200112L

/* No bsdsocket.library prototypes, no inline calls: <proto/socket.h> is
   their entry, and its guard keeps <sys/socket.h> from including it. The
   inline select() of <sys/select.h> and the inline inet_ntoa() and
   relatives of <arpa/inet.h> call the library too; the guards below keep
   them out, and aos_compat.h declares the POSIX functions instead. */
#define PROTO_SOCKET_H   1
#define _SELECT_DECLARED 1
#define _INET_H_         1

#include <stdint.h>
#include <sys/types.h>
#include <machine/endian.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/uio.h>
/* struct msghdr of libnix has caddr_t (char *) members where POSIX has
   void *, and code that assigns a struct sockaddr * to them does not
   compile. The libnix structure is renamed here and replaced below by one
   with the same layout. */
#define msghdr aos_libnix_msghdr
#include <sys/socket.h>
#undef msghdr
#include <netinet/in.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <wchar.h>
#include <signal.h>

/* <unistd.h> of libnix includes <proto/exec.h>, whose inline calls are
   macros; these names are also used by the linphone stack (a class Alert,
   methods Remove() and Enqueue()). */
#undef Alert
#undef Remove
#undef Enqueue

/* <wchar.h> of libnix defines mbrlen() as a two-argument macro over the
   three-argument function it also declares. */
#undef mbrlen

/* POSIX types that libnix does not define. */
typedef uint32_t socklen_t;
typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;
typedef uint8_t sa_family_t;
typedef uint32_t id_t;

#ifndef SHUT_RD
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2
#endif

/* Sockets never raise signals on MorphOS, so the flag has nothing to do. */
#define MSG_NOSIGNAL 0

struct msghdr {
    void *msg_name;
    socklen_t msg_namelen;
    struct iovec *msg_iov;
    unsigned int msg_iovlen;
    void *msg_control;
    socklen_t msg_controllen;
    int msg_flags;
};

/* Control messages: CMSG_NXTHDR() of libnix uses ALIGN() without defining
   it and does arithmetic on msg_control; CMSG_SPACE() and CMSG_LEN() are
   missing. */
#ifndef ALIGN
#define ALIGN(p) (((unsigned long)(p) + 3UL) & ~3UL)
#endif
#undef CMSG_NXTHDR
#define CMSG_NXTHDR(mhdr, cmsg)                                    \
    (((char *)(cmsg) + (cmsg)->cmsg_len + sizeof(struct cmsghdr) > \
      (char *)(mhdr)->msg_control + (mhdr)->msg_controllen)        \
         ? (struct cmsghdr *)NULL                                  \
         : (struct cmsghdr *)((char *)(cmsg) + ALIGN((cmsg)->cmsg_len)))
#define CMSG_SPACE(len) (ALIGN(sizeof(struct cmsghdr)) + ALIGN(len))
#define CMSG_LEN(len)   (ALIGN(sizeof(struct cmsghdr)) + (len))

#ifndef alloca
#define alloca(size) __builtin_alloca(size)
#endif

/* IPv6, with the BSD structure layout of the other socket addresses and
   the FreeBSD values; AF_INET6 is above the libnix AF_MAX. */
#define AF_INET6 28
#undef AF_MAX
#define AF_MAX              29 /* tables indexed by family (belle-sip dns.c) */
#define PF_INET6            AF_INET6
#define IPPROTO_IPV6        41
#define INET6_ADDRSTRLEN    46
#define IPV6_UNICAST_HOPS   4
#define IPV6_MULTICAST_IF   9
#define IPV6_MULTICAST_HOPS 10
#define IPV6_MULTICAST_LOOP 11
#define IPV6_JOIN_GROUP     12
#define IPV6_LEAVE_GROUP    13
#define IPV6_V6ONLY         27
#define IPV6_RECVPKTINFO    36

struct in6_addr {
    union {
        uint8_t u6_addr8[16];
        uint16_t u6_addr16[8];
        uint32_t u6_addr32[4];
    } u6_addr;
};
#define s6_addr   u6_addr.u6_addr8
#define s6_addr16 u6_addr.u6_addr16
#define s6_addr32 u6_addr.u6_addr32

struct sockaddr_in6 {
    uint8_t sin6_len;
    sa_family_t sin6_family;
    in_port_t sin6_port;
    uint32_t sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t sin6_scope_id;
};

struct ipv6_mreq {
    struct in6_addr ipv6mr_multiaddr;
    unsigned int ipv6mr_interface;
};

struct sockaddr_storage {
    uint8_t ss_len;
    sa_family_t ss_family;
    char ss_pad1[6];
    int64_t ss_align;
    char ss_pad2[112];
};

#define IN6ADDR_ANY_INIT                                       \
    {                                                          \
        {                                                      \
            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } \
        }                                                      \
    }
#define IN6ADDR_LOOPBACK_INIT                                  \
    {                                                          \
        {                                                      \
            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 } \
        }                                                      \
    }

#define IN6_IS_ADDR_UNSPECIFIED(a)                                                 \
    ((a)->s6_addr32[0] == 0 && (a)->s6_addr32[1] == 0 && (a)->s6_addr32[2] == 0 && \
     (a)->s6_addr32[3] == 0)
#define IN6_IS_ADDR_LOOPBACK(a)                                                    \
    ((a)->s6_addr32[0] == 0 && (a)->s6_addr32[1] == 0 && (a)->s6_addr32[2] == 0 && \
     (a)->s6_addr32[3] == htonl(1))
#define IN6_IS_ADDR_V4MAPPED(a) \
    ((a)->s6_addr32[0] == 0 && (a)->s6_addr32[1] == 0 && (a)->s6_addr32[2] == htonl(0xffff))
#define IN6_IS_ADDR_MULTICAST(a) ((a)->s6_addr[0] == 0xff)
#define IN6_IS_ADDR_LINKLOCAL(a) ((a)->s6_addr[0] == 0xfe && ((a)->s6_addr[1] & 0xc0) == 0x80)
#define IN6_IS_ADDR_SITELOCAL(a) ((a)->s6_addr[0] == 0xfe && ((a)->s6_addr[1] & 0xc0) == 0xc0)

/* getaddrinfo() and getnameinfo(), which aos_compat.h declares. */
struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    socklen_t ai_addrlen;
    char *ai_canonname;
    struct sockaddr *ai_addr;
    struct addrinfo *ai_next;
};

#define AI_PASSIVE     0x0001
#define AI_CANONNAME   0x0002
#define AI_NUMERICHOST 0x0004
#define AI_NUMERICSERV 0x0008
#define AI_ALL         0x0100
#define AI_ADDRCONFIG  0x0400
#define AI_V4MAPPED    0x0800

#define EAI_AGAIN    2
#define EAI_BADFLAGS 3
#define EAI_FAIL     4
#define EAI_FAMILY   5
#define EAI_MEMORY   6
#define EAI_NONAME   8
#define EAI_SERVICE  9
#define EAI_SOCKTYPE 10
#define EAI_SYSTEM   11
#define EAI_OVERFLOW 14

#define NI_MAXHOST     1025
#define NI_MAXSERV     32
#define NI_NOFQDN      0x01
#define NI_NUMERICHOST 0x02
#define NI_NAMEREQD    0x04
#define NI_NUMERICSERV 0x08
#define NI_DGRAM       0x10

#ifdef __cplusplus
extern "C" {
#endif

/* In libpthread, but declared by no libnix header. */
int pthread_sigmask(int how, const sigset_t *set, sigset_t *oset);

/* From libaoscompat: strptime(), which libnix lacks, and the POSIX
   timezone variable (seconds west of UTC). libnix declares timezone as the
   old BSD function in <time.h>, so the name becomes a call here; the code
   that uses timezone only reads it, and struct timezone appears only in
   <sys/time.h>, included above. */
struct tm;
char *strptime(const char *s, const char *format, struct tm *tm);
long aos_timezone(void);
#define timezone (aos_timezone())

/* Defined by libaosnet. */
extern const struct in6_addr in6addr_any;
extern const struct in6_addr in6addr_loopback;

#ifdef __cplusplus
}
#endif

#endif /* AOS_COMPAT_MORPHOS_H */
