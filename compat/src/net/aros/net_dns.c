/*
 * DNS servers configured in AROSTCP. The linphone resolver looks for them
 * in /etc/resolv.conf, which AROS does not have; the application passes
 * this list to linphone_core_set_dns_servers() instead.
 */
#include "net_internal.h"

#include "aos/net.h"

#include <exec/lists.h>
#include <libraries/bsdsocket.h>
#include <string.h>

int aos_net_dns_servers(char (*out)[AOS_NET_ADDRSTRLEN], int max) {
    AOS_NET_REQUIRE_BASE(-1);
    if (max < 0 || (max > 0 && out == NULL)) {
        errno = EINVAL;
        return -1;
    }

    struct List *list = __ObtainDomainNameServerList_WB(SocketBase);
    if (list == NULL) {
        errno = ENOMEM;
        return -1;
    }

    int count = 0;
    for (struct Node *n = list->lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
        const struct DomainNameServerNode *dns = (const struct DomainNameServerNode *)n;
        if (dns->dnsn_Address == NULL || dns->dnsn_Address[0] == '\0') {
            continue;
        }
        if (count < max) {
            strncpy(out[count], (const char *)dns->dnsn_Address, AOS_NET_ADDRSTRLEN - 1);
            out[count][AOS_NET_ADDRSTRLEN - 1] = '\0';
        }
        ++count;
    }
    __ReleaseDomainNameServerList_WB(SocketBase, list);
    return count;
}
