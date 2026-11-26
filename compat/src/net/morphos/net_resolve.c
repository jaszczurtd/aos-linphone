/*
 * Address conversion and name resolution for MorphOS.
 *
 * NetStack has the AmiTCP database calls only (gethostbyname(),
 * gethostbyaddr(), getservbyname(), getservbyport()), which return data in
 * the library base that every thread shares; they are called under one
 * lock and their results copied at once. getaddrinfo() and getnameinfo()
 * are built on them. NetStack has no IPv6, but IPv6 literals are converted
 * (inet_pton(), inet_ntop(), numeric getaddrinfo()), since the linphone
 * stack parses addresses with these functions; such addresses cannot be
 * used with a socket.
 */
#include "net_internal.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Declared by compat_morphos.h. */
const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

/* ---- IPv4 and IPv6 text ---- */

/* inet_aton() accepts the BSD forms a.b.c.d, a.b.c, a.b and a, each part
   decimal, octal (leading 0) or hexadecimal (leading 0x). */
int inet_aton(const char *cp, struct in_addr *addr) {
    uint32_t parts[4];
    int n = 0;
    const char *p = cp;
    if (cp == NULL) {
        return 0;
    }
    for (;;) {
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
        char *end;
        unsigned long v = strtoul(p, &end, 0);
        if (end == p || v > 0xffffffffUL || n == 4) {
            return 0;
        }
        parts[n++] = (uint32_t)v;
        p = end;
        if (*p == '.') {
            ++p;
            continue;
        }
        if (*p != '\0' && !isspace((unsigned char)*p)) {
            return 0;
        }
        break;
    }
    uint32_t a;
    switch (n) {
    case 1:
        a = parts[0];
        break;
    case 2:
        if (parts[0] > 0xff || parts[1] > 0xffffff) {
            return 0;
        }
        a = parts[0] << 24 | parts[1];
        break;
    case 3:
        if (parts[0] > 0xff || parts[1] > 0xff || parts[2] > 0xffff) {
            return 0;
        }
        a = parts[0] << 24 | parts[1] << 16 | parts[2];
        break;
    default:
        if (parts[0] > 0xff || parts[1] > 0xff || parts[2] > 0xff || parts[3] > 0xff) {
            return 0;
        }
        a = parts[0] << 24 | parts[1] << 16 | parts[2] << 8 | parts[3];
        break;
    }
    if (addr != NULL) {
        addr->s_addr = htonl(a);
    }
    return 1;
}

in_addr_t inet_addr(const char *cp) {
    struct in_addr a;
    return inet_aton(cp, &a) ? a.s_addr : INADDR_NONE;
}

/* Shared by all threads, like the static buffer of a POSIX inet_ntoa(). */
char *inet_ntoa(struct in_addr in) {
    static char buf[16];
    const unsigned char *b = (const unsigned char *)&in.s_addr;
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return buf;
}

/* Strict dotted decimal, four parts, as inet_pton(AF_INET) requires. */
static int aos_pton4(const char *src, unsigned char *dst) {
    int parts = 0;
    unsigned int value = 0;
    int digits = 0;
    unsigned char out[4];
    for (const char *p = src;; ++p) {
        if (*p >= '0' && *p <= '9') {
            if (digits > 0 && value == 0) {
                return 0; /* leading zero */
            }
            value = value * 10 + (unsigned int)(*p - '0');
            if (value > 255 || ++digits > 3) {
                return 0;
            }
        } else if ((*p == '.' || *p == '\0') && digits > 0 && parts < 4) {
            out[parts++] = (unsigned char)value;
            value = 0;
            digits = 0;
            if (*p == '\0') {
                break;
            }
        } else {
            return 0;
        }
    }
    if (parts != 4) {
        return 0;
    }
    memcpy(dst, out, 4);
    return 1;
}

static int aos_hex(int c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = tolower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* RFC 4291 text form, with "::" and a trailing dotted IPv4 part. */
static int aos_pton6(const char *src, unsigned char *dst) {
    unsigned char out[16];
    int len = 0, gap = -1;
    const char *p = src;
    memset(out, 0, sizeof(out));
    if (p[0] == ':') {
        if (p[1] != ':') {
            return 0;
        }
        ++p;
    }
    while (*p != '\0') {
        if (*p == ':') {
            if (gap >= 0) {
                return 0; /* a second "::" */
            }
            gap = len;
            ++p;
            continue;
        }
        const char *start = p;
        unsigned int value = 0;
        int digits = 0;
        for (int h; (h = aos_hex((unsigned char)*p)) >= 0; ++p) {
            value = value << 4 | (unsigned int)h;
            if (++digits > 4) {
                return 0;
            }
        }
        if (*p == '.') {
            if (len > 12 || !aos_pton4(start, out + len)) {
                return 0;
            }
            len += 4;
            p = start + strlen(start);
            break;
        }
        if (digits == 0 || len > 14) {
            return 0;
        }
        out[len++] = (unsigned char)(value >> 8);
        out[len++] = (unsigned char)value;
        if (*p == ':') {
            ++p;
            if (*p == '\0') {
                return 0; /* trailing single ':' */
            }
            if (*p == ':') {
                continue; /* "::" handled at the top of the loop */
            }
        } else if (*p != '\0') {
            return 0;
        }
    }
    if (gap >= 0) {
        if (len == 16) {
            return 0;
        }
        memmove(out + gap + (16 - len), out + gap, (size_t)(len - gap));
        memset(out + gap, 0, (size_t)(16 - len));
    } else if (len != 16) {
        return 0;
    }
    memcpy(dst, out, 16);
    return 1;
}

int inet_pton(int af, const char *src, void *dst) {
    if (src == NULL || dst == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (af == AF_INET) {
        return aos_pton4(src, dst);
    }
    if (af == AF_INET6) {
        return aos_pton6(src, dst);
    }
    errno = EAFNOSUPPORT;
    return -1;
}

static const char *aos_ntop4(const unsigned char *a, char *dst, socklen_t size) {
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
    if (n < 0 || (socklen_t)n >= size) {
        errno = ENOSPC;
        return NULL;
    }
    memcpy(dst, buf, (size_t)n + 1);
    return dst;
}

/* RFC 5952: lower case, the longest run of two or more zero groups as
   "::", IPv4-mapped and IPv4-compatible addresses with a dotted tail. */
static const char *aos_ntop6(const unsigned char *a, char *dst, socklen_t size) {
    unsigned int g[8];
    for (int i = 0; i < 8; ++i) {
        g[i] = (unsigned int)a[2 * i] << 8 | a[2 * i + 1];
    }
    int best = -1, best_len = 0;
    for (int i = 0; i < 8;) {
        if (g[i] != 0) {
            ++i;
            continue;
        }
        int j = i;
        while (j < 8 && g[j] == 0) {
            ++j;
        }
        if (j - i > best_len && j - i >= 2) {
            best = i;
            best_len = j - i;
        }
        i = j;
    }
    int v4_tail = best == 0 && (best_len == 6 || (best_len == 5 && g[5] == 0xffff));

    char buf[INET6_ADDRSTRLEN];
    size_t n = 0;
    for (int i = 0; i < 8; ++i) {
        if (i == best) {
            buf[n++] = ':';
            if (i == 0) {
                buf[n++] = ':';
            }
            i += best_len - 1;
            continue;
        }
        if (v4_tail && i == 6) {
            n += (size_t)snprintf(buf + n, sizeof(buf) - n, "%u.%u.%u.%u", a[12], a[13], a[14], a[15]);
            break;
        }
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, "%x", g[i]);
        if (i < 7) {
            buf[n++] = ':';
        }
    }
    buf[n] = '\0';
    if (n >= size) {
        errno = ENOSPC;
        return NULL;
    }
    memcpy(dst, buf, n + 1);
    return dst;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size) {
    if (src == NULL || dst == NULL) {
        errno = EFAULT;
        return NULL;
    }
    if (af == AF_INET) {
        return aos_ntop4(src, dst, size);
    }
    if (af == AF_INET6) {
        return aos_ntop6(src, dst, size);
    }
    errno = EAFNOSUPPORT;
    return NULL;
}

/* ---- getaddrinfo() ---- */

#define AOS_GAI_MAX_ADDRS 16

struct aos_gai_addr {
    int family;
    unsigned char addr[16];
};

/* One result entry with its address; the canonical name, when any, follows
   the first entry. */
struct aos_ai_node {
    struct addrinfo ai;
    struct sockaddr_storage addr;
};

static int aos_gai_herr(int herr) {
    switch (herr) {
    case TRY_AGAIN:
        return EAI_AGAIN;
    case HOST_NOT_FOUND:
    case NO_DATA:
        return EAI_NONAME;
    default:
        return EAI_FAIL;
    }
}

/* The port of a service in network order, or -1 with *err set. */
static int aos_gai_port(const char *service, int socktype, int flags, int *err) {
    if (service == NULL) {
        return 0;
    }
    char *end;
    unsigned long port = strtoul(service, &end, 10);
    if (*service != '\0' && *end == '\0') {
        if (port > 65535) {
            *err = EAI_SERVICE;
            return -1;
        }
        return htons((uint16_t)port);
    }
    if (flags & AI_NUMERICSERV) {
        *err = EAI_NONAME;
        return -1;
    }
    aos_ns_db_lock();
    struct servent *s = aos_ns_getservbyname(service, socktype == SOCK_DGRAM ? "udp" : "tcp");
    int result = s != NULL ? (int)(uint16_t)s->s_port : -1;
    aos_ns_db_unlock();
    if (result < 0) {
        *err = EAI_SERVICE;
    }
    return result;
}

/* Fills addrs with the addresses of node; returns their number (0 or more)
   or a negative EAI_* code. canon receives the canonical name when asked. */
static int aos_gai_lookup(const char *node, int flags, struct aos_gai_addr *addrs, char *canon, size_t canon_size) {
    if (node == NULL) {
        int passive = (flags & AI_PASSIVE) != 0;
        addrs[0].family = AF_INET;
        uint32_t v4 = htonl(passive ? INADDR_ANY : INADDR_LOOPBACK);
        memcpy(addrs[0].addr, &v4, 4);
        addrs[1].family = AF_INET6;
        memcpy(addrs[1].addr, passive ? &in6addr_any : &in6addr_loopback, 16);
        return 2;
    }
    struct in_addr a4;
    if (inet_aton(node, &a4)) {
        addrs[0].family = AF_INET;
        memcpy(addrs[0].addr, &a4, 4);
        return 1;
    }
    if (aos_pton6(node, addrs[0].addr)) {
        addrs[0].family = AF_INET6;
        return 1;
    }
    if (flags & AI_NUMERICHOST) {
        return -EAI_NONAME;
    }

    int herr = 0, count = 0;
    aos_ns_db_lock();
    struct hostent *h = aos_ns_gethostbyname(node, &herr);
    if (h != NULL && h->h_addrtype == AF_INET && h->h_length == 4) {
        for (char **p = h->h_addr_list; *p != NULL && count < AOS_GAI_MAX_ADDRS; ++p) {
            addrs[count].family = AF_INET;
            memcpy(addrs[count].addr, *p, 4);
            ++count;
        }
        if (canon != NULL && h->h_name != NULL) {
            snprintf(canon, canon_size, "%s", h->h_name);
        }
    }
    aos_ns_db_unlock();
    if (h == NULL) {
        return -aos_gai_herr(herr);
    }
    return count;
}

/* A result entry for one address and socket type, in the family asked
   for (an IPv4 address becomes IPv4-mapped IPv6 when map is set). */
static struct addrinfo *aos_gai_node(const struct aos_gai_addr *a, int map, int socktype, int protocol, int port,
                                     const char *canon) {
    size_t canon_len = canon != NULL ? strlen(canon) + 1 : 0;
    struct aos_ai_node *node = calloc(1, sizeof(*node) + canon_len);
    if (node == NULL) {
        return NULL;
    }
    if (a->family == AF_INET && !map) {
        struct sockaddr_in *in4 = (struct sockaddr_in *)&node->addr;
        in4->sin_len = sizeof(*in4);
        in4->sin_family = AF_INET;
        in4->sin_port = (in_port_t)port;
        memcpy(&in4->sin_addr, a->addr, 4);
        node->ai.ai_family = AF_INET;
        node->ai.ai_addrlen = sizeof(*in4);
    } else {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&node->addr;
        in6->sin6_len = sizeof(*in6);
        in6->sin6_family = AF_INET6;
        in6->sin6_port = (in_port_t)port;
        if (a->family == AF_INET) {
            in6->sin6_addr.s6_addr[10] = 0xff;
            in6->sin6_addr.s6_addr[11] = 0xff;
            memcpy(&in6->sin6_addr.s6_addr[12], a->addr, 4);
        } else {
            memcpy(&in6->sin6_addr, a->addr, 16);
        }
        node->ai.ai_family = AF_INET6;
        node->ai.ai_addrlen = sizeof(*in6);
    }
    node->ai.ai_socktype = socktype;
    node->ai.ai_protocol = protocol;
    node->ai.ai_addr = (struct sockaddr *)&node->addr;
    if (canon_len != 0) {
        node->ai.ai_canonname = (char *)(node + 1);
        memcpy(node->ai.ai_canonname, canon, canon_len);
    }
    return &node->ai;
}

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) {
    if (res == NULL) {
        return EAI_FAIL;
    }
    *res = NULL;
    int flags = hints != NULL ? hints->ai_flags : 0;
    int family = hints != NULL ? hints->ai_family : AF_UNSPEC;
    int socktype = hints != NULL ? hints->ai_socktype : 0;
    int protocol = hints != NULL ? hints->ai_protocol : 0;
    if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6) {
        return EAI_FAMILY;
    }
    if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM && socktype != SOCK_RAW) {
        return EAI_SOCKTYPE;
    }
    if (node == NULL && service == NULL) {
        return EAI_NONAME;
    }
    if (!aos_ns_available()) {
        errno = ENETDOWN;
        return EAI_SYSTEM;
    }

    int err = 0;
    int port = aos_gai_port(service, socktype, flags, &err);
    if (port < 0) {
        return err;
    }

    struct aos_gai_addr addrs[AOS_GAI_MAX_ADDRS];
    char canon[256] = "";
    int count = aos_gai_lookup(node, flags, addrs, (flags & AI_CANONNAME) ? canon : NULL, sizeof(canon));
    if (count < 0) {
        return -count;
    }

    /* Socket types: the one asked for, or stream and datagram. */
    int types[2][2] = {{SOCK_STREAM, IPPROTO_TCP}, {SOCK_DGRAM, IPPROTO_UDP}};
    int ntypes = 2;
    if (socktype == 0 && protocol == IPPROTO_TCP) {
        socktype = SOCK_STREAM; /* as glibc: the protocol picks the type */
    } else if (socktype == 0 && protocol == IPPROTO_UDP) {
        socktype = SOCK_DGRAM;
    }
    if (socktype != 0) {
        types[0][0] = socktype;
        types[0][1] = protocol != 0 ? protocol
                                    : (socktype == SOCK_STREAM ? IPPROTO_TCP : (socktype == SOCK_DGRAM ? IPPROTO_UDP : 0));
        ntypes = 1;
    }

    struct addrinfo *result = NULL, **tail = &result;
    const char *name = canon[0] != '\0' ? canon : ((flags & AI_CANONNAME) ? node : NULL);
    for (int i = 0; i < count; ++i) {
        int map = 0;
        if (family == AF_INET && addrs[i].family != AF_INET) {
            continue;
        }
        if (family == AF_INET6 && addrs[i].family == AF_INET) {
            /* AI_V4MAPPED applies to host names: without one, [::] only. */
            if (!(flags & AI_V4MAPPED) || node == NULL) {
                continue;
            }
            map = 1;
        }
        if (family == AF_UNSPEC && node == NULL && addrs[i].family == AF_INET6) {
            continue; /* no IPv6 sockets: the IPv4 wildcard or loopback only */
        }
        for (int t = 0; t < ntypes; ++t) {
            struct addrinfo *ai = aos_gai_node(&addrs[i], map, types[t][0], types[t][1], port, name);
            if (ai == NULL) {
                freeaddrinfo(result);
                return EAI_MEMORY;
            }
            name = NULL;
            *tail = ai;
            tail = &ai->ai_next;
        }
    }
    if (result == NULL) {
        return EAI_NONAME;
    }
    *res = result;
    return 0;
}

void freeaddrinfo(struct addrinfo *res) {
    while (res != NULL) {
        struct addrinfo *next = res->ai_next;
        free(res);
        res = next;
    }
}

/* ---- getnameinfo() ---- */

static int aos_copy_name(char *dst, socklen_t size, const char *src) {
    size_t len = strlen(src);
    if (len >= size) {
        return EAI_OVERFLOW;
    }
    memcpy(dst, src, len + 1);
    return 0;
}

int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen, char *serv,
                socklen_t servlen, int flags) {
    if (sa == NULL) {
        return EAI_FAIL;
    }
    const void *addr;
    int port;
    if (sa->sa_family == AF_INET && salen >= sizeof(struct sockaddr_in)) {
        addr = &((const struct sockaddr_in *)sa)->sin_addr;
        port = ntohs(((const struct sockaddr_in *)sa)->sin_port);
    } else if (sa->sa_family == AF_INET6 && salen >= sizeof(struct sockaddr_in6)) {
        addr = &((const struct sockaddr_in6 *)sa)->sin6_addr;
        port = ntohs(((const struct sockaddr_in6 *)sa)->sin6_port);
    } else {
        return EAI_FAMILY;
    }

    if (host != NULL && hostlen > 0) {
        int done = 0;
        if (!(flags & NI_NUMERICHOST) && sa->sa_family == AF_INET && aos_ns_available()) {
            char name[NI_MAXHOST];
            int herr = 0;
            aos_ns_db_lock();
            struct hostent *h = aos_ns_gethostbyaddr(addr, 4, AF_INET, &herr);
            if (h != NULL && h->h_name != NULL) {
                snprintf(name, sizeof(name), "%s", h->h_name);
                done = 1;
            }
            aos_ns_db_unlock();
            if (done) {
                if (flags & NI_NOFQDN) {
                    char *dot = strchr(name, '.');
                    if (dot != NULL) {
                        *dot = '\0';
                    }
                }
                int err = aos_copy_name(host, hostlen, name);
                if (err != 0) {
                    return err;
                }
            }
        }
        if (!done) {
            if (flags & NI_NAMEREQD) {
                return EAI_NONAME;
            }
            if (inet_ntop(sa->sa_family, addr, host, hostlen) == NULL) {
                return EAI_OVERFLOW;
            }
        }
    }

    if (serv != NULL && servlen > 0) {
        char name[NI_MAXSERV] = "";
        if (!(flags & NI_NUMERICSERV) && aos_ns_available()) {
            aos_ns_db_lock();
            struct servent *s = aos_ns_getservbyport(htons((uint16_t)port), (flags & NI_DGRAM) ? "udp" : "tcp");
            if (s != NULL && s->s_name != NULL) {
                snprintf(name, sizeof(name), "%s", s->s_name);
            }
            aos_ns_db_unlock();
        }
        if (name[0] == '\0') {
            snprintf(name, sizeof(name), "%d", port);
        }
        int err = aos_copy_name(serv, servlen, name);
        if (err != 0) {
            return err;
        }
    }
    return 0;
}

const char *gai_strerror(int errcode) {
    switch (errcode) {
    case 0:
        return "Success";
    case EAI_AGAIN:
        return "Temporary failure in name resolution";
    case EAI_BADFLAGS:
        return "Invalid flags";
    case EAI_FAIL:
        return "Non-recoverable failure in name resolution";
    case EAI_FAMILY:
        return "Address family not supported";
    case EAI_MEMORY:
        return "Out of memory";
    case EAI_NONAME:
        return "Name or service not known";
    case EAI_SERVICE:
        return "Service not supported for socket type";
    case EAI_SOCKTYPE:
        return "Socket type not supported";
    case EAI_SYSTEM:
        return "System error";
    case EAI_OVERFLOW:
        return "Buffer too small";
    default:
        return "Unknown error";
    }
}
