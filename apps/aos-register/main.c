/*
 * aos-register - registers a SIP account from AROS with liblinphone, then
 * unregisters it.
 *
 * usage: aos-register <identity> <password> [<proxy>] [<seconds>]
 *
 *   identity  SIP address of the account, e.g. sip:alice@192.168.122.1
 *   password  its password
 *   proxy     registrar address, default: the identity's domain over UDP
 *   seconds   time allowed for each step, default 20
 *
 * The core runs with IPv6 disabled (AROSTCP cannot send to IPv4-mapped
 * addresses) and takes the DNS servers from AROSTCP. The SIP parsers read
 * their grammars at run time from belr/grammars/ in the resource directory,
 * PROGDIR:share, so the *.belr files from <prefix>/share/belr/grammars must
 * sit in share/belr/grammars next to the program. The core keeps its files
 * in RAM:aos-register.
 *
 * Exit code: 0 when the account registered and unregistered, 10 when a step
 * failed or timed out, 20 on bad arguments or a missing network.
 */
#include "aos/net.h"

#include <linphone/core.h>
#include <linphone/factory.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DATA_DIR "RAM:aos-register"
#define RESOURCES_DIR "PROGDIR:share"

static const char *state_name(LinphoneRegistrationState state)
{
    switch (state) {
    case LinphoneRegistrationNone: return "None";
    case LinphoneRegistrationProgress: return "Progress";
    case LinphoneRegistrationOk: return "Ok";
    case LinphoneRegistrationCleared: return "Cleared";
    case LinphoneRegistrationFailed: return "Failed";
    case LinphoneRegistrationRefreshing: return "Refreshing";
    }
    return "?";
}

/* Runs the core until the account reaches the wanted state, fails or the
   time runs out. Returns 1 when the wanted state was reached. */
static int run_until(LinphoneCore *core, LinphoneAccount *account,
                     LinphoneRegistrationState wanted, int seconds)
{
    LinphoneRegistrationState last = (LinphoneRegistrationState)-1;
    time_t end = time(NULL) + seconds;
    while (time(NULL) < end) {
        linphone_core_iterate(core);
        LinphoneRegistrationState state = linphone_account_get_state(account);
        if (state != last) {
            printf("registration state: %s\n", state_name(state));
            last = state;
        }
        if (state == wanted) {
            return 1;
        }
        if (state == LinphoneRegistrationFailed) {
            return 0;
        }
        usleep(20000);
    }
    printf("timeout waiting for %s\n", state_name(wanted));
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <identity> <password> [<proxy>] [<seconds>]\n", argv[0]);
        return 20;
    }
    const char *identity = argv[1];
    const char *password = argv[2];
    const char *proxy = argc > 3 ? argv[3] : NULL;
    int seconds = argc > 4 ? atoi(argv[4]) : 20;

    if (aos_net_status() != 0) {
        fprintf(stderr, "no network: bsdsocket.library is not available\n");
        return 20;
    }

    LinphoneFactory *factory = linphone_factory_get();
    linphone_logging_service_set_log_level(linphone_logging_service_get(), LinphoneLogLevelWarning);
    linphone_factory_set_top_resources_dir(factory, RESOURCES_DIR);
    mkdir(DATA_DIR, 0755);
    linphone_factory_set_data_dir(factory, DATA_DIR);
    linphone_factory_set_config_dir(factory, DATA_DIR);
    linphone_factory_set_download_dir(factory, DATA_DIR);

    LinphoneCore *core = linphone_factory_create_core_3(factory, NULL, NULL, NULL);
    if (core == NULL) {
        fprintf(stderr, "cannot create the linphone core\n");
        return 10;
    }
    linphone_core_enable_ipv6(core, FALSE);

    /* SIP over UDP on a random port only. */
    LinphoneTransports *transports = linphone_factory_create_transports(factory);
    linphone_transports_set_udp_port(transports, LC_SIP_TRANSPORT_RANDOM);
    linphone_transports_set_tcp_port(transports, LC_SIP_TRANSPORT_DISABLED);
    linphone_transports_set_tls_port(transports, LC_SIP_TRANSPORT_DISABLED);
    linphone_core_set_transports(core, transports);
    linphone_transports_unref(transports);

    /* The linphone resolver would look for /etc/resolv.conf. */
    char servers[4][AOS_NET_ADDRSTRLEN];
    int count = aos_net_dns_servers(servers, 4);
    bctbx_list_t *dns = NULL;
    for (int i = 0; i < count && i < 4; ++i) {
        dns = bctbx_list_append(dns, servers[i]);
        printf("DNS server: %s\n", servers[i]);
    }
    linphone_core_set_dns_servers(core, dns);
    bctbx_list_free(dns);

    LinphoneAddress *id_addr = linphone_factory_create_address(factory, identity);
    if (id_addr == NULL) {
        fprintf(stderr, "invalid identity: %s\n", identity);
        linphone_core_unref(core);
        return 20;
    }
    char proxy_buf[256];
    if (proxy == NULL) {
        snprintf(proxy_buf, sizeof(proxy_buf), "sip:%s;transport=udp", linphone_address_get_domain(id_addr));
        proxy = proxy_buf;
    }
    LinphoneAddress *proxy_addr = linphone_factory_create_address(factory, proxy);
    if (proxy_addr == NULL) {
        fprintf(stderr, "invalid proxy: %s\n", proxy);
        linphone_address_unref(id_addr);
        linphone_core_unref(core);
        return 20;
    }

    LinphoneAuthInfo *auth = linphone_factory_create_auth_info_2(
        factory, linphone_address_get_username(id_addr), NULL, password, NULL, NULL,
        linphone_address_get_domain(id_addr), NULL);
    linphone_core_add_auth_info(core, auth);
    linphone_auth_info_unref(auth);

    LinphoneAccountParams *params = linphone_core_create_account_params(core);
    linphone_account_params_set_identity_address(params, id_addr);
    linphone_account_params_set_server_address(params, proxy_addr);
    linphone_account_params_enable_register(params, TRUE);
    LinphoneAccount *account = linphone_core_create_account(core, params);
    linphone_account_params_unref(params);
    linphone_core_add_account(core, account);
    linphone_address_unref(id_addr);
    linphone_address_unref(proxy_addr);

    printf("registering %s through %s\n", identity, proxy);
    linphone_core_start(core);
    int registered = run_until(core, account, LinphoneRegistrationOk, seconds);
    int unregistered = 0;
    if (registered) {
        printf("REGISTERED\n");
        LinphoneAccountParams *off = linphone_account_params_clone(linphone_account_get_params(account));
        linphone_account_params_enable_register(off, FALSE);
        linphone_account_set_params(account, off);
        linphone_account_params_unref(off);
        unregistered = run_until(core, account, LinphoneRegistrationCleared, seconds);
        if (unregistered) {
            printf("UNREGISTERED\n");
        }
    }

    linphone_account_unref(account);
    linphone_core_stop(core);
    linphone_core_unref(core);
    printf("result: %s\n", registered && unregistered ? "PASS" : "FAIL");
    return registered && unregistered ? 0 : 10;
}
