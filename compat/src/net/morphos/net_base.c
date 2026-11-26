/*
 * libaosnet functions without a POSIX counterpart, for MorphOS.
 *
 * bsdsocket.library is opened by libauto before main() and shared by all
 * threads; NetStack keeps descriptors per task (net_fd.c).
 *
 * NetStack has no call that lists the DNS servers it got from DHCP, and it
 * does not write them to a file. The servers set by hand in the network
 * preferences are in ENV:sys/net/resolv.conf as "NAMESERVER <address>"
 * lines, which is all aos_net_dns_servers() can report.
 */
#include "net_internal.h"

#include "aos/net.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define AOS_RESOLV_CONF "ENV:sys/net/resolv.conf"

int aos_net_status(void) {
    return aos_ns_available() ? 0 : ENETDOWN;
}

int aos_net_dns_servers(char (*out)[AOS_NET_ADDRSTRLEN], int max) {
    if (!aos_ns_available()) {
        errno = ENETDOWN;
        return -1;
    }
    if (max < 0 || (max > 0 && out == NULL)) {
        errno = EINVAL;
        return -1;
    }

    FILE *f = fopen(AOS_RESOLV_CONF, "r");
    if (f == NULL) {
        return 0;
    }
    int count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
        char *p = line;
        while (isspace((unsigned char)*p)) {
            ++p;
        }
        /* ";" starts a comment; the keyword is case-insensitive. */
        if (strncasecmp(p, "NAMESERVER", 10) != 0 || !isspace((unsigned char)p[10])) {
            continue;
        }
        p += 10;
        while (isspace((unsigned char)*p)) {
            ++p;
        }
        size_t len = 0;
        while (p[len] != '\0' && !isspace((unsigned char)p[len]) && p[len] != ';') {
            ++len;
        }
        struct in_addr a;
        char addr[AOS_NET_ADDRSTRLEN];
        if (len == 0 || len >= sizeof(addr)) {
            continue;
        }
        memcpy(addr, p, len);
        addr[len] = '\0';
        if (inet_pton(AF_INET, addr, &a) != 1) {
            continue;
        }
        if (count < max) {
            memcpy(out[count], addr, len + 1);
        }
        ++count;
    }
    fclose(f);
    return count;
}
