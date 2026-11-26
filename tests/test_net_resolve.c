/*
 * Address conversion, getaddrinfo() with the emulated AI_V4MAPPED and
 * AI_ALL flags, getnameinfo(), resolution from a second thread and the DNS
 * server list.
 */
#include "aos_test.h"

#include "aos/net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void check_conversion(void) {
    struct in_addr a4;
    struct in6_addr a6;
    char text[INET6_ADDRSTRLEN];

    AOS_CHECK(inet_pton(AF_INET, "192.168.122.1", &a4) == 1 &&
                  a4.s_addr == htonl(0xc0a87a01),
              "inet_pton() of an IPv4 address");
    AOS_CHECK(inet_pton(AF_INET6, "fe80::1:2", &a6) == 1 && a6.s6_addr[0] == 0xfe &&
                  a6.s6_addr[15] == 2,
              "inet_pton() of an IPv6 address");
    AOS_CHECK(inet_pton(AF_INET, "300.1.1.1", &a4) == 0, "inet_pton() rejects a bad address");
    errno = 0;
    AOS_CHECK(inet_pton(12345, "1.2.3.4", &a4) == -1 && errno == EAFNOSUPPORT,
              "inet_pton() of an unknown family fails with EAFNOSUPPORT");

    AOS_CHECK(inet_ntop(AF_INET6, &a6, text, sizeof(text)) != NULL && strcmp(text, "fe80::1:2") == 0,
              "inet_ntop() of an IPv6 address");
    errno = 0;
    AOS_CHECK(inet_ntop(AF_INET, &a4, text, 4) == NULL && errno == ENOSPC,
              "inet_ntop() into a short buffer fails with ENOSPC");

    struct in_addr lo = {htonl(INADDR_LOOPBACK)};
    AOS_CHECK(strcmp(inet_ntoa(lo), "127.0.0.1") == 0, "inet_ntoa()");
    AOS_CHECK(inet_addr("10.0.0.7") == htonl(0x0a000007), "inet_addr()");
    AOS_CHECK(inet_addr("nonsense") == INADDR_NONE, "inet_addr() of a bad address");
    AOS_CHECK(inet_aton("10.1.2.3", &a4) == 1 && a4.s_addr == htonl(0x0a010203), "inet_aton()");

    static const unsigned char zero[16];
    AOS_CHECK(memcmp(&in6addr_any, zero, 16) == 0, "in6addr_any is ::");
    AOS_CHECK(memcmp(&in6addr_loopback, zero, 15) == 0 && in6addr_loopback.s6_addr[15] == 1,
              "in6addr_loopback is ::1");

    /* An IPv6 address in s6_addr32 words (bctoolbox reads it this way). */
    AOS_CHECK(inet_pton(AF_INET6, "::ffff:1.2.3.4", &a6) == 1 && a6.s6_addr32[2] == htonl(0xffff) &&
                  a6.s6_addr32[3] == htonl(0x01020304),
              "s6_addr32 view of an IPv4-mapped address");
}

static int count_entries(const struct addrinfo *ai) {
    int n = 0;
    for (; ai != NULL; ai = ai->ai_next) {
        ++n;
    }
    return n;
}

static void check_getaddrinfo(void) {
    struct addrinfo hints, *res = NULL;
    char host[NI_MAXHOST], serv[NI_MAXSERV];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    int err = getaddrinfo("127.0.0.1", "5060", &hints, &res);
    AOS_CHECK(err == 0 && res != NULL && res->ai_family == AF_INET &&
                  ((struct sockaddr_in *)res->ai_addr)->sin_port == htons(5060),
              "getaddrinfo() of a numeric IPv4 address and port");
    if (err == 0) {
        AOS_CHECK(getnameinfo(res->ai_addr, (socklen_t)res->ai_addrlen, host, sizeof(host), serv,
                              sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV) == 0 &&
                      strcmp(host, "127.0.0.1") == 0 && strcmp(serv, "5060") == 0,
                  "getnameinfo() back to numeric strings");
        freeaddrinfo(res);
    }

    /* bctbx_name_to_addrinfo(): AF_INET6 with AI_V4MAPPED | AI_ALL */
    hints.ai_family = AF_INET6;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | AI_V4MAPPED | AI_ALL;
    res = NULL;
    err = getaddrinfo("192.168.1.10", "5060", &hints, &res);
    const struct sockaddr_in6 *in6 = res != NULL ? (const struct sockaddr_in6 *)res->ai_addr : NULL;
    AOS_CHECK(err == 0 && count_entries(res) == 1 && res->ai_family == AF_INET6 &&
                  res->ai_addrlen == sizeof(struct sockaddr_in6) && in6->sin6_port == htons(5060) &&
                  in6->sin6_addr.s6_addr32[2] == htonl(0xffff) &&
                  in6->sin6_addr.s6_addr32[3] == htonl(0xc0a8010a),
              "AI_V4MAPPED returns an IPv4 address as ::ffff:a.b.c.d");
    if (err == 0) {
        AOS_CHECK(getnameinfo(res->ai_addr, (socklen_t)res->ai_addrlen, host, sizeof(host), NULL, 0,
                              NI_NUMERICHOST) == 0 &&
                      strcmp(host, "::ffff:192.168.1.10") == 0,
                  "getnameinfo() of the mapped address");
        freeaddrinfo(res);
    }

    res = NULL;
    err = getaddrinfo("::1", "5060", &hints, &res);
    AOS_CHECK(err == 0 && count_entries(res) == 1 && res->ai_family == AF_INET6 &&
                  ((const struct sockaddr_in6 *)res->ai_addr)->sin6_addr.s6_addr[15] == 1,
              "AI_V4MAPPED | AI_ALL keeps an IPv6 address as it is");
    if (err == 0) {
        freeaddrinfo(res);
    }

    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | AI_V4MAPPED;
    res = NULL;
    err = getaddrinfo("192.168.1.10", "5060", &hints, &res);
    AOS_CHECK(err == 0 && count_entries(res) == 1 && res->ai_family == AF_INET6 &&
                  ((const struct sockaddr_in6 *)res->ai_addr)->sin6_addr.s6_addr32[3] == htonl(0xc0a8010a),
              "AI_V4MAPPED alone maps an address that has no IPv6 form");
    if (err == 0) {
        freeaddrinfo(res);
    }

    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    res = NULL;
    err = getaddrinfo("192.168.1.10", "5060", &hints, &res);
    AOS_CHECK(err != 0 && res == NULL, "AF_INET6 without AI_V4MAPPED rejects an IPv4 address");
    AOS_CHECK(gai_strerror(err) != NULL, "gai_strerror() describes the error");

    hints.ai_family = AF_UNSPEC;
    res = NULL;
    err = getaddrinfo("not..an.address", "5060", &hints, &res);
    AOS_CHECK(err != 0 && res == NULL, "AI_NUMERICHOST rejects a name");

    hints.ai_flags = 0;
    res = NULL;
    err = getaddrinfo("localhost", NULL, &hints, &res);
    printf("info: getaddrinfo(\"localhost\"): %d (%s), %d entries\n", err,
           err ? gai_strerror(err) : "ok", count_entries(res));
    if (err == 0) {
        freeaddrinfo(res);
    }
    int herr = h_errno;
    printf("info: h_errno after the lookups: %d\n", herr);
}

/* Counts the entries of a lookup of name; -1 when it fails. */
static int lookup_count(const char *name, int family, long flags, int *mapped) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = flags;
    if (getaddrinfo(name, "5060", &hints, &res) != 0) {
        return -1;
    }
    int n = 0;
    *mapped = 0;
    for (const struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next, ++n) {
        if (ai->ai_family == AF_INET6 &&
            ((const struct sockaddr_in6 *)ai->ai_addr)->sin6_addr.s6_addr32[2] == htonl(0xffff)) {
            ++*mapped;
        }
    }
    freeaddrinfo(res);
    return n;
}

/* The AI_V4MAPPED rules on a name, whatever AROSTCP's host table holds. */
static void check_v4mapped_rules(void) {
    int mapped = 0;
    int n6 = lookup_count("localhost", AF_INET6, 0, &mapped);
    int n4 = lookup_count("localhost", AF_INET, 0, &mapped);
    printf("info: localhost has %d IPv6 and %d IPv4 entries\n", n6 < 0 ? 0 : n6, n4 < 0 ? 0 : n4);
    n6 = n6 < 0 ? 0 : n6;
    n4 = n4 < 0 ? 0 : n4;
    AOS_CHECK(n6 + n4 > 0, "localhost resolves");

    int n = lookup_count("localhost", AF_INET6, AI_V4MAPPED, &mapped);
    if (n6 > 0) {
        AOS_CHECK(n == n6 && mapped == 0, "AI_V4MAPPED adds no IPv4 entry when IPv6 ones exist");
    } else {
        AOS_CHECK(n == n4 && mapped == n4, "AI_V4MAPPED maps the IPv4 entries when no IPv6 one exists");
    }
    n = lookup_count("localhost", AF_INET6, AI_V4MAPPED | AI_ALL, &mapped);
    AOS_CHECK(n == n6 + n4 && mapped == n4, "AI_V4MAPPED | AI_ALL returns both, IPv4 ones mapped");

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE | AI_V4MAPPED | AI_ALL;
    int err = getaddrinfo(NULL, "5060", &hints, &res);
    AOS_CHECK(err == 0 && count_entries(res) == 1 && res->ai_family == AF_INET6,
              "a passive lookup without a host returns only [::]");
    if (err == 0) {
        freeaddrinfo(res);
    }
}

struct resolve_job {
    struct addrinfo *res;
    int err;
};

static void *resolve_in_thread(void *arg) {
    struct resolve_job *job = arg;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_flags = AI_NUMERICHOST;
    job->err = getaddrinfo("10.20.30.40", "80", &hints, &job->res);
    return NULL;
}

static void check_thread_resolve(void) {
    struct resolve_job job = {NULL, -1};
    pthread_t th;
    pthread_create(&th, NULL, resolve_in_thread, &job);
    pthread_join(th, NULL);
    AOS_CHECK(job.err == 0 && job.res != NULL &&
                  ((struct sockaddr_in *)job.res->ai_addr)->sin_addr.s_addr == htonl(0x0a141e28),
              "getaddrinfo() in a thread that did not open the library");
    /* Freed by another thread than the one that resolved. */
    freeaddrinfo(job.res);
}

static void check_host_and_dns(void) {
    char name[256];
    AOS_CHECK(gethostname(name, sizeof(name)) == 0 && name[0] != '\0', "gethostname()");
    printf("info: host name \"%s\"\n", name);

    char servers[4][AOS_NET_ADDRSTRLEN];
    int count = aos_net_dns_servers(servers, 4);
    printf("info: %d DNS server(s)%s%s\n", count, count > 0 ? ", first " : "",
           count > 0 ? servers[0] : "");
    struct in_addr a;
#ifdef __MORPHOS__
    /* NetStack does not report the servers it got from DHCP; the list has
       only the servers set by hand (ENV:sys/net/resolv.conf). */
    AOS_CHECK(count >= 0 && (count == 0 || inet_pton(AF_INET, servers[0], &a) == 1),
              "aos_net_dns_servers() lists the servers set by hand");
#else
    AOS_CHECK(count >= 1 && inet_pton(AF_INET, servers[0], &a) == 1,
              "aos_net_dns_servers() lists the DHCP DNS server");
#endif
    AOS_CHECK(aos_net_dns_servers(NULL, 0) == count, "aos_net_dns_servers() counts without copying");
    errno = 0;
    AOS_CHECK(aos_net_dns_servers(servers, -1) == -1 && errno == EINVAL,
              "aos_net_dns_servers() rejects a negative capacity");
}

int main(void) {
    AOS_CHECK(aos_net_status() == 0, "bsdsocket.library opened before main()");
    if (aos_net_status() != 0) {
        return aos_test_result();
    }
    check_conversion();
    check_getaddrinfo();
    check_v4mapped_rules();
    check_thread_resolve();
    check_host_and_dns();
    return aos_test_result();
}
