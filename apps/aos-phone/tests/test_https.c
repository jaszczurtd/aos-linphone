/*
 * Tests of HTTPS on AROS through belle-sip's HTTP client, which liblinphone
 * uses for its account services (linphone.org account creation): TCP,
 * TLS with mbedtls, and certificate verification against a CA file.
 *
 * Needs AROSTCP with a DNS server and access to the internet, and the belr
 * grammars in share/belr/grammars next to the program.
 *
 *   test_https [<https URL> [<CA file>]]
 *
 * The URL defaults to https://subscribe.linphone.org/api/ping, which answers
 * "pong", and the CA file to PROGDIR:share/linphone/rootca.pem, the root
 * certificates the SDK installs.
 *
 * Checked: a GET over TLS with the certificate verified gives status 200 and
 * the expected body; the same GET with only an unrelated CA trusted fails
 * instead of answering; a plain HTTP GET over TCP gets an answer.
 */
#include "aos_test.h"

#include "aos/net.h"

#include <belle-sip/belle-sip.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TIMEOUT_MS 20000

/* A self-signed CA made for this test only: it signed no server. */
static const char UNTRUSTED_CA[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBrTCCAVOgAwIBAgIUbmbYjVTsuG9dkUY2fhazRMFIIzcwCgYIKoZIzj0EAwIw\n"
    "KzEpMCcGA1UEAwwgYW9zLWxpbnBob25lIHRlc3QgQ0EgKHVudHJ1c3RlZCkwIBcN\n"
    "MjYxMTI1MTg1OTA1WhgPMjEyNjExMDExODU5MDVaMCsxKTAnBgNVBAMMIGFvcy1s\n"
    "aW5waG9uZSB0ZXN0IENBICh1bnRydXN0ZWQpMFkwEwYHKoZIzj0CAQYIKoZIzj0D\n"
    "AQcDQgAEBEBluC1XSQLSLEuaDBc/KPKVFEz3owckGIo3EJeoEUyPy1WUWDS5cK9j\n"
    "Izal0yr62xbx4EF3yx2HKTLTOEyqJKNTMFEwHQYDVR0OBBYEFBw7oHxQLhs0F0CP\n"
    "qUAKe3wh93A7MB8GA1UdIwQYMBaAFBw7oHxQLhs0F0CPqUAKe3wh93A7MA8GA1Ud\n"
    "EwEB/wQFMAMBAf8wCgYIKoZIzj0EAwIDSAAwRQIhAIojY+z6eWdDpwjEHeBmax4m\n"
    "t0LkmNWjZDcQiYuoy3UPAiAhaEAJnjqZvL21vYcen/ZfSZCFs8C+pI+pNeFYXcjJ\n"
    "yg==\n"
    "-----END CERTIFICATE-----\n";

static struct {
    int done;
    int status;   /* HTTP status, 0 when none */
    int io_error; /* the connection failed (also on a rejected certificate) */
    int timeout;
    char body[256];
} result;

static void on_response(void *ctx, const belle_http_response_event_t *event) {
    (void)ctx;
    result.status = belle_http_response_get_status_code(event->response);
    const char *body = belle_sip_message_get_body(BELLE_SIP_MESSAGE(event->response));
    snprintf(result.body, sizeof(result.body), "%s", body != NULL ? body : "");
    result.done = 1;
}

static void on_io_error(void *ctx, const belle_sip_io_error_event_t *event) {
    (void)ctx;
    (void)event;
    result.io_error = 1;
    result.done = 1;
}

static void on_timeout(void *ctx, const belle_sip_timeout_event_t *event) {
    (void)ctx;
    (void)event;
    result.timeout = 1;
    result.done = 1;
}

/* GETs url with the given CA (file path, or PEM data when pem is set);
   returns the time it took in milliseconds, -1 when nothing came. */
static long get(belle_sip_stack_t *stack, const char *url, const char *ca, int pem) {
    memset(&result, 0, sizeof(result));
    belle_http_provider_t *provider =
        belle_sip_stack_create_http_provider_with_transports(stack, "0.0.0.0", BELLE_SIP_HTTP_TRANSPORT_TCP | BELLE_SIP_HTTP_TRANSPORT_TLS);
    belle_tls_crypto_config_t *crypto = belle_tls_crypto_config_new();
    if (pem) {
        belle_tls_crypto_config_set_root_ca_data(crypto, ca);
    } else {
        belle_tls_crypto_config_set_root_ca(crypto, ca);
    }
    belle_http_provider_set_tls_crypto_config(provider, crypto);
    belle_http_request_listener_callbacks_t cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.process_response = on_response;
    cbs.process_io_error = on_io_error;
    cbs.process_timeout = on_timeout;
    belle_http_request_listener_t *listener = belle_http_request_listener_create_from_callbacks(&cbs, NULL);
    belle_http_request_t *request = belle_http_request_create("GET", belle_generic_uri_parse(url), NULL);
    long start = aos_test_now_ms();
    belle_http_provider_send_request(provider, request, listener);
    while (!result.done && aos_test_now_ms() - start < TIMEOUT_MS) {
        belle_sip_stack_sleep(stack, 50);
    }
    long took = result.done ? aos_test_now_ms() - start : -1;
    belle_sip_object_unref(listener);
    belle_sip_object_unref(provider);
    belle_sip_object_unref(crypto);
    return took;
}

int main(int argc, char **argv) {
    /* belle-sip parses HTTP with the belr grammars. Without a linphone core
       nothing adds PROGDIR:share/belr/grammars to the grammar paths, but
       belr also looks in share/belr/grammars below the current directory. */
    chdir("PROGDIR:");
    const char *url = argc > 1 ? argv[1] : "https://subscribe.linphone.org/api/ping";
    const char *ca = argc > 2 ? argv[2] : "PROGDIR:share/linphone/rootca.pem";
    AOS_CHECK(aos_net_status() == 0, "bsdsocket.library opened before main()");
    if (aos_net_status() != 0) {
        return aos_test_result();
    }
    char servers[4][AOS_NET_ADDRSTRLEN];
    int count = aos_net_dns_servers(servers, 4);
    belle_sip_list_t *dns = NULL;
    for (int i = 0; i < count && i < 4; ++i) {
        dns = belle_sip_list_append(dns, servers[i]);
    }
    belle_sip_stack_t *stack = belle_sip_stack_new(NULL);
    belle_sip_stack_set_dns_servers(stack, dns);
    belle_sip_list_free(dns);

    long took = get(stack, url, ca, 0);
    printf("info: GET %s: status %d, body \"%s\", %ld ms, io error %d, timeout %d\n", url, result.status,
           result.body, took, result.io_error, result.timeout);
    AOS_CHECK(result.status == 200, "an HTTPS GET with the certificate verified answers 200");
    if (argc <= 1) {
        AOS_CHECK(strcmp(result.body, "pong") == 0, "the HTTPS answer carries the expected body");
    }

    took = get(stack, url, UNTRUSTED_CA, 1);
    printf("info: GET %s trusting an unrelated CA: status %d, %ld ms, io error %d, timeout %d\n", url,
           result.status, took, result.io_error, result.timeout);
    AOS_CHECK(result.status == 0 && result.done, "the certificate of the server is rejected when its CA is not trusted");

    char plain[256];
    snprintf(plain, sizeof(plain), "http://%s", strncmp(url, "https://", 8) == 0 ? url + 8 : url);
    took = get(stack, plain, ca, 0);
    printf("info: GET %s: status %d, %ld ms\n", plain, result.status, took);
    AOS_CHECK(result.status > 0, "a plain HTTP GET over TCP gets an answer");

    belle_sip_object_unref(stack);
    return aos_test_result();
}
