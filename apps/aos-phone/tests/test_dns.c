/*
 * Tests of name resolution on AROS and MorphOS, the way the phone resolves
 * names: getaddrinfo() of a real host name through the compatibility layer,
 * and the DNS resolver of belle-sip, which the linphone core uses for SIP
 * domains, given the DNS servers of the network configuration
 * (aos_net_dns_servers()), as aos-phone does.
 *
 * Needs AROSTCP with a DNS server that answers for the internet.
 *
 *   test_dns [<host> [<sip domain>]]
 *
 * <host> needs an A record and <sip domain> _sip._udp SRV records; both
 * default to sip.linphone.org.
 *
 * Checked: getaddrinfo() of the host gives an IPv4 address with the port
 * asked for, and fails for a name under .invalid; belle-sip finds the A
 * record of the host, the SRV records of the domain, and the addresses of
 * the SRV targets in one query, as for a SIP domain, and reports a name
 * under .invalid as not found; every query ends within its timeout. With
 * no DNS server known (MorphOS takes its servers from DHCP and does not
 * report them), belle-sip resolves addresses through getaddrinfo() and the
 * SIP domain through its A record.
 */
#include "aos_test.h"

#include "aos/net.h"

#include <belle-sip/belle-sip.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#define TIMEOUT_MS  10000
#define MISSING     "aos-test.invalid" /* RFC 2606: never resolves */
#define MAX_RESULTS 8

static struct {
    int done;
    int addresses;          /* IPv4 addresses received */
    int port_ok;            /* every address has the port asked for */
    int srv_records;        /* SRV records received */
    int addresses_with_srv; /* addresses that belong to an SRV record */
    char first[INET_ADDRSTRLEN];
    char targets[MAX_RESULTS][128];
    unsigned short ports[MAX_RESULTS];
} result;

static int expected_port;

static void reset(int port) {
    memset(&result, 0, sizeof(result));
    result.port_ok = 1;
    expected_port = port;
}

static void take_addresses(const struct addrinfo *ai, const belle_sip_resolver_results_t *results) {
    for (; ai != NULL; ai = ai->ai_next) {
        if (ai->ai_family != AF_INET) {
            continue;
        }
        const struct sockaddr_in *sin = (const struct sockaddr_in *)ai->ai_addr;
        if (result.addresses == 0) {
            inet_ntop(AF_INET, &sin->sin_addr, result.first, sizeof(result.first));
        }
        ++result.addresses;
        if (expected_port > 0 && ntohs(sin->sin_port) != expected_port) {
            result.port_ok = 0;
        }
        if (results != NULL && belle_sip_resolver_results_get_srv_from_addrinfo(results, ai) != NULL) {
            ++result.addresses_with_srv;
        }
    }
}

static void on_addresses(void *data, belle_sip_resolver_results_t *results) {
    (void)data;
    take_addresses(belle_sip_resolver_results_get_addrinfos(results), results);
    result.srv_records = (int)bctbx_list_size(belle_sip_resolver_results_get_srv_records(results));
    result.done = 1;
}

static void on_srv(void *data, const char *name, belle_sip_list_t *srv_list, uint32_t ttl) {
    (void)data;
    (void)name;
    (void)ttl;
    for (const belle_sip_list_t *it = srv_list; it != NULL; it = it->next) {
        const belle_sip_dns_srv_t *srv = it->data;
        if (result.srv_records < MAX_RESULTS) {
            snprintf(result.targets[result.srv_records], sizeof(result.targets[0]), "%s",
                     belle_sip_dns_srv_get_target(srv));
            result.ports[result.srv_records] = belle_sip_dns_srv_get_port(srv);
        }
        ++result.srv_records;
    }
    result.done = 1;
}

/* Runs the stack until the callback has come or the timeout passed;
   returns the time it took in milliseconds, or -1 on timeout. */
static long wait_result(belle_sip_stack_t *stack) {
    long start = aos_test_now_ms();
    while (!result.done && aos_test_now_ms() - start < TIMEOUT_MS) {
        belle_sip_stack_sleep(stack, 50);
    }
    return result.done ? aos_test_now_ms() - start : -1;
}

static void test_getaddrinfo(const char *host) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    long start = aos_test_now_ms();
    int err = getaddrinfo(host, "5060", &hints, &res);
    long took = aos_test_now_ms() - start;
    reset(5060);
    if (err == 0) {
        take_addresses(res, NULL);
        freeaddrinfo(res);
    }
    printf("info: getaddrinfo(%s): %d address(es), first %s, %ld ms\n", host, result.addresses, result.first, took);
    AOS_CHECK(err == 0 && result.addresses > 0 && result.port_ok,
              "getaddrinfo() of a host name gives its IPv4 address with the port");

    res = NULL;
    start = aos_test_now_ms();
    err = getaddrinfo(MISSING, "5060", &hints, &res);
    took = aos_test_now_ms() - start;
    if (err == 0) {
        freeaddrinfo(res);
    }
    printf("info: getaddrinfo(%s): error %d, %ld ms\n", MISSING, err, took);
    AOS_CHECK(err != 0, "getaddrinfo() of a name that does not exist fails");
}

static void test_belle_sip(const char *host, const char *domain) {
    char servers[4][AOS_NET_ADDRSTRLEN];
    int count = aos_net_dns_servers(servers, 4);
    belle_sip_list_t *dns = NULL;
    for (int i = 0; i < count && i < 4; ++i) {
        dns = belle_sip_list_append(dns, servers[i]);
    }
    belle_sip_stack_t *stack = belle_sip_stack_new(NULL);
    belle_sip_stack_set_dns_servers(stack, dns);
    belle_sip_list_free(dns);

    reset(5060);
    belle_sip_stack_resolve_a(stack, host, 5060, AF_INET, on_addresses, NULL);
    long took = wait_result(stack);
    printf("info: belle-sip A %s: %d address(es), first %s, %ld ms\n", host, result.addresses, result.first, took);
    AOS_CHECK(took >= 0 && result.addresses > 0 && result.port_ok, "belle-sip finds the A record of a host");

    reset(0);
    belle_sip_stack_resolve_srv(stack, "sip", "udp", domain, on_srv, NULL);
    took = wait_result(stack);
    printf("info: belle-sip SRV _sip._udp.%s: %d record(s), %ld ms\n", domain, result.srv_records, took);
    int targets_ok = result.srv_records > 0;
    for (int i = 0; i < result.srv_records && i < MAX_RESULTS; ++i) {
        printf("info:   %s:%u\n", result.targets[i], (unsigned)result.ports[i]);
        targets_ok = targets_ok && result.targets[i][0] != '\0' && result.ports[i] != 0;
    }
    if (count > 0) {
        AOS_CHECK(took >= 0 && targets_ok, "belle-sip finds the SRV records of a SIP domain");
    } else {
        /* belle-sip looks up SRV records with its own resolver only, which
           needs a DNS server; addresses come from getaddrinfo(). */
        printf("info: no DNS server known: no SRV records\n");
    }

    reset(0);
    belle_sip_stack_resolve(stack, "sip", "udp", domain, 5060, AF_INET, on_addresses, NULL);
    took = wait_result(stack);
    printf("info: belle-sip SRV and A of %s: %d address(es) (%d from SRV targets), %d SRV record(s), first %s, "
           "%ld ms\n",
           domain, result.addresses, result.addresses_with_srv, result.srv_records, result.first, took);
    if (count > 0) {
        AOS_CHECK(took >= 0 && result.addresses > 0 && result.srv_records > 0 && result.addresses_with_srv > 0,
                  "belle-sip resolves a SIP domain through its SRV records to addresses");
    } else {
        AOS_CHECK(took >= 0 && result.addresses > 0, "without a DNS server, a SIP domain resolves through its A record");
    }

    reset(5060);
    belle_sip_stack_resolve_a(stack, MISSING, 5060, AF_INET, on_addresses, NULL);
    took = wait_result(stack);
    printf("info: belle-sip A %s: %d address(es), %ld ms\n", MISSING, result.addresses, took);
    AOS_CHECK(took >= 0 && result.addresses == 0, "belle-sip reports a name that does not exist without addresses");

    belle_sip_object_unref(stack);
}

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "sip.linphone.org";
    const char *domain = argc > 2 ? argv[2] : "sip.linphone.org";
    AOS_CHECK(aos_net_status() == 0, "bsdsocket.library opened before main()");
    if (aos_net_status() != 0) {
        return aos_test_result();
    }
    char servers[4][AOS_NET_ADDRSTRLEN];
    int count = aos_net_dns_servers(servers, 4);
    printf("info: %d DNS server(s)%s%s\n", count, count > 0 ? ", first " : "", count > 0 ? servers[0] : "");
#ifdef __MORPHOS__
    /* The MorphOS network stack reports only the servers set by hand. */
    printf("info: %s\n", count > 0 ? "DNS servers set by hand" : "no DNS server known");
#else
    AOS_CHECK(count > 0, "the network configuration gives a DNS server");
#endif
    test_getaddrinfo(host);
    test_belle_sip(host, domain);
    return aos_test_result();
}
