/*
 * Address conversion and name resolution over the shared bsdsocket.library
 * base. AROSTCP implements them inside the library (RS_getaddrinfo() and
 * friends), with POSIX return values and EAI_* codes from <netdb.h>.
 */
#include "net_internal.h"

#include <limits.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>

/* Declared by <netinet/in.h>; the AROS SDK defines them only in net.lib. */
const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

/* LONG is 32-bit on AROS, unlike the C type long. */
#define AOS_NET_LONG_MAX 0x7fffffffL

static LONG aos_net_size(socklen_t size)
{
    return size > (socklen_t)AOS_NET_LONG_MAX ? (LONG)AOS_NET_LONG_MAX : (LONG)size;
}

in_addr_t inet_addr(const char *cp)
{
    AOS_NET_REQUIRE_BASE(INADDR_NONE);
    return __inet_addr_WB(SocketBase, cp);
}

int inet_aton(const char *cp, struct in_addr *addr)
{
    AOS_NET_REQUIRE_BASE(0);
    return __inet_aton_WB(SocketBase, (CONST_STRPTR)cp, addr) ? 1 : 0;
}

/* Returns a buffer inside the library, shared by all threads like the
   static buffer of a POSIX inet_ntoa(). */
char *inet_ntoa(struct in_addr in)
{
    AOS_NET_REQUIRE_BASE(NULL);
    return __Inet_NtoA_WB(SocketBase, in.s_addr);
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    AOS_NET_REQUIRE_BASE(NULL);
    return (const char *)__inet_ntop_WB(SocketBase, af, (void *)src, dst, aos_net_size(size));
}

int inet_pton(int af, const char *src, void *dst)
{
    AOS_NET_REQUIRE_BASE(-1);
    return __inet_pton_WB(SocketBase, af, src, dst);
}

/* One node of a result list returned by getaddrinfo(): the entry, its
   address and, after the structure, the canonical name. */
struct aos_ai_node {
    struct addrinfo ai;
    struct sockaddr_storage addr;
};

/* Copies one library entry into a node of our own. With map_v4, an IPv4
   entry becomes an IPv4-mapped IPv6 one (::ffff:a.b.c.d). */
static struct addrinfo *aos_ai_copy(const struct addrinfo *src, int map_v4)
{
    size_t canon = src->ai_canonname != NULL ? strlen(src->ai_canonname) + 1 : 0;
    struct aos_ai_node *node;

    if (src->ai_addrlen > sizeof(node->addr)) {
        return NULL;
    }
    node = calloc(1, sizeof(*node) + canon);
    if (node == NULL) {
        return NULL;
    }
    node->ai = *src;
    node->ai.ai_next = NULL;
    node->ai.ai_addr = (struct sockaddr *)&node->addr;
    node->ai.ai_canonname = NULL;
    if (map_v4 && src->ai_family == AF_INET) {
        const struct sockaddr_in *in4 = (const struct sockaddr_in *)src->ai_addr;
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&node->addr;
        in6->sin6_len = sizeof(*in6);
        in6->sin6_family = AF_INET6;
        in6->sin6_port = in4->sin_port;
        in6->sin6_addr.s6_addr[10] = 0xff;
        in6->sin6_addr.s6_addr[11] = 0xff;
        memcpy(&in6->sin6_addr.s6_addr[12], &in4->sin_addr, 4);
        node->ai.ai_family = AF_INET6;
        node->ai.ai_addrlen = sizeof(*in6);
    } else {
        memcpy(&node->addr, src->ai_addr, src->ai_addrlen);
    }
    if (canon != 0) {
        node->ai.ai_canonname = (char *)(node + 1);
        memcpy(node->ai.ai_canonname, src->ai_canonname, canon);
    }
    return &node->ai;
}

/* Appends copies of a library list at *tail; returns 0 or EAI_MEMORY. */
static int aos_ai_append(struct addrinfo ***tail, const struct addrinfo *list, int map_v4)
{
    for (; list != NULL; list = list->ai_next) {
        struct addrinfo *copy = aos_ai_copy(list, map_v4);
        if (copy == NULL) {
            return EAI_MEMORY;
        }
        **tail = copy;
        *tail = &copy->ai_next;
    }
    return 0;
}

/*
 * The results are copied into memory of our own and the library lists are
 * released at once, so freeaddrinfo() is a plain free() whatever the thread
 * and whether or not an entry was converted.
 *
 * AROSTCP does not know AI_V4MAPPED and AI_ALL (aos_compat.h defines them),
 * so they are done here as POSIX describes: with ai_family AF_INET6 and a
 * host name, AI_V4MAPPED adds IPv4 addresses as IPv4-mapped IPv6 ones when
 * the host has no IPv6 address, and AI_ALL | AI_V4MAPPED adds them in any
 * case. A failed IPv6 lookup other than "no such address" (EAI_AGAIN,
 * EAI_SYSTEM...) is returned as it is, without a second query.
 */
int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
    if (res == NULL) {
        return EAI_FAIL;
    }
    *res = NULL;
    if (SocketBase == NULL) {
        errno = aos_net_open_error;
        return EAI_SYSTEM;
    }

    struct addrinfo local_hints;
    long flags = 0;
    if (hints != NULL) {
        local_hints = *hints;
        flags = hints->ai_flags;
        local_hints.ai_flags &= ~(long)(AI_V4MAPPED | AI_ALL);
        hints = &local_hints;
    }
    int map_v4 = node != NULL && hints != NULL && hints->ai_family == AF_INET6 &&
                 (flags & AI_V4MAPPED);

    struct addrinfo *list = NULL;
    int err = __getaddrinfo_WB(SocketBase, node, service, hints, &list);

    int no_v6 = (err == 0 && list == NULL) || err == EAI_NONAME || err == EAI_NODATA ||
                err == EAI_FAMILY || err == EAI_ADDRFAMILY;
    struct addrinfo *v4 = NULL;
    int err_v4 = EAI_NONAME;
    if (map_v4 && (no_v6 || (err == 0 && (flags & AI_ALL)))) {
        struct addrinfo v4_hints = local_hints;
        v4_hints.ai_family = AF_INET;
        err_v4 = __getaddrinfo_WB(SocketBase, node, service, &v4_hints, &v4);
    }

    struct addrinfo *result = NULL;
    struct addrinfo **tail = &result;
    int status = 0;
    if (err == 0) {
        status = aos_ai_append(&tail, list, 0);
    }
    if (status == 0 && err_v4 == 0) {
        status = aos_ai_append(&tail, v4, 1);
    }
    if (list != NULL) {
        __freeaddrinfo_WB(SocketBase, list);
    }
    if (v4 != NULL) {
        __freeaddrinfo_WB(SocketBase, v4);
    }

    if (status != 0) {
        freeaddrinfo(result);
        return status;
    }
    if (result == NULL) {
        return err != 0 ? err : (err_v4 != 0 ? err_v4 : EAI_NONAME);
    }
    *res = result;
    return 0;
}

void freeaddrinfo(struct addrinfo *res)
{
    while (res != NULL) {
        struct addrinfo *next = res->ai_next;
        free(res);
        res = next;
    }
}

int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags)
{
    if (SocketBase == NULL) {
        errno = aos_net_open_error;
        return EAI_SYSTEM;
    }
    return __getnameinfo_WB(SocketBase, (struct sockaddr *)sa, aos_net_size(salen),
                            host, aos_net_size(hostlen),
                            serv, aos_net_size(servlen), flags);
}

const char *gai_strerror(int errcode)
{
    if (SocketBase == NULL) {
        return "Network unavailable (bsdsocket.library not open)";
    }
    return (const char *)__gai_strerror_WB(SocketBase, errcode);
}
