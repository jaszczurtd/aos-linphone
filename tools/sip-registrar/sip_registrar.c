/*
 * sip-registrar - a minimal SIP registrar for testing account registration.
 *
 * Listens on UDP, answers REGISTER with a digest challenge (MD5, qop=auth),
 * accepts the request once the credentials match, and records bindings and
 * their removal. Everything else gets 200 (OPTIONS) or 501. It keeps no
 * state between runs and serves one account.
 *
 * usage: sip-registrar [-a address] [-p port] [-u user] [-w password]
 *                      [-r realm] [-t seconds] [-x]
 *
 *   -x  exit with status 0 as soon as the account has registered and then
 *       unregistered (expires 0); otherwise run until -t seconds pass
 *       (default 120) and exit 1 if that never happened.
 *
 * Each event is printed as one line: REGISTERED <aor> <contact> <expires>,
 * UNREGISTERED <aor>, CHALLENGE, REJECTED <reason>.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAX_MSG     8192
#define MAX_HEADERS 64

struct header {
    char name[64];
    char value[1024];
};

struct request {
    char method[32];
    char uri[512];
    struct header headers[MAX_HEADERS];
    int count;
};

struct config {
    const char *user;
    const char *password;
    const char *realm;
    char nonce[33];
};

static void md5_hex(const char *text, char out[33]) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_Digest(text, strlen(text), digest, &len, EVP_md5(), NULL);
    for (unsigned int i = 0; i < len && i < 16; ++i) {
        sprintf(out + 2 * i, "%02x", digest[i]);
    }
    out[32] = '\0';
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') {
        ++s;
    }
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) {
        *--e = '\0';
    }
    return s;
}

/* Full header names for the compact forms SIP allows. */
static const char *expand_name(const char *name) {
    static const struct {
        const char *compact, *full;
    } map[] = {
        {"v", "Via"},
        {"f", "From"},
        {"t", "To"},
        {"i", "Call-ID"},
        {"m", "Contact"},
        {"l", "Content-Length"},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcasecmp(name, map[i].compact) == 0) {
            return map[i].full;
        }
    }
    return name;
}

static int parse_request(char *msg, struct request *req) {
    memset(req, 0, sizeof(*req));
    char *line = strtok(msg, "\n");
    if (line == NULL || sscanf(line, "%31s %511s SIP/2.0", req->method, req->uri) != 2) {
        return -1;
    }
    while ((line = strtok(NULL, "\n")) != NULL) {
        line = trim(line);
        if (*line == '\0') {
            break; /* end of headers */
        }
        char *colon = strchr(line, ':');
        if (colon == NULL || req->count == MAX_HEADERS) {
            continue;
        }
        *colon = '\0';
        struct header *h = &req->headers[req->count++];
        snprintf(h->name, sizeof(h->name), "%s", expand_name(trim(line)));
        snprintf(h->value, sizeof(h->value), "%s", trim(colon + 1));
    }
    return 0;
}

static const char *get_header(const struct request *req, const char *name) {
    for (int i = 0; i < req->count; ++i) {
        if (strcasecmp(req->headers[i].name, name) == 0) {
            return req->headers[i].value;
        }
    }
    return NULL;
}

/* Value of a parameter in a digest header: name="value" or name=value. */
static int digest_param(const char *header, const char *name, char *out, size_t size) {
    size_t n = strlen(name);
    for (const char *p = header; (p = strcasestr(p, name)) != NULL; p += n) {
        if (p != header && isalnum((unsigned char)p[-1])) {
            continue;
        }
        const char *v = p + n;
        while (*v == ' ') {
            ++v;
        }
        if (*v != '=') {
            continue;
        }
        ++v;
        while (*v == ' ') {
            ++v;
        }
        size_t len;
        if (*v == '"') {
            ++v;
            len = strcspn(v, "\"");
        } else {
            len = strcspn(v, ", \t");
        }
        if (len >= size) {
            return -1;
        }
        memcpy(out, v, len);
        out[len] = '\0';
        return 0;
    }
    return -1;
}

static int check_digest(const struct config *cfg, const struct request *req, const char *auth) {
    char user[128], realm[128], nonce[128], uri[512], response[64];
    char qop[32] = "", nc[32] = "", cnonce[128] = "";
    if (strncasecmp(auth, "Digest", 6) != 0 ||
        digest_param(auth, "username", user, sizeof(user)) != 0 ||
        digest_param(auth, "realm", realm, sizeof(realm)) != 0 ||
        digest_param(auth, "nonce", nonce, sizeof(nonce)) != 0 ||
        digest_param(auth, "uri", uri, sizeof(uri)) != 0 ||
        digest_param(auth, "response", response, sizeof(response)) != 0) {
        return 0;
    }
    digest_param(auth, "qop", qop, sizeof(qop));
    digest_param(auth, "nc", nc, sizeof(nc));
    digest_param(auth, "cnonce", cnonce, sizeof(cnonce));
    if (strcmp(user, cfg->user) != 0 || strcmp(realm, cfg->realm) != 0 ||
        strcmp(nonce, cfg->nonce) != 0) {
        return 0;
    }

    char buf[1200], ha1[33], ha2[33], expected[33];
    snprintf(buf, sizeof(buf), "%s:%s:%s", user, realm, cfg->password);
    md5_hex(buf, ha1);
    snprintf(buf, sizeof(buf), "%s:%s", req->method, uri);
    md5_hex(buf, ha2);
    if (qop[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s:%s:%s:%s:%s:%s", ha1, nonce, nc, cnonce, qop, ha2);
    } else {
        snprintf(buf, sizeof(buf), "%s:%s:%s", ha1, nonce, ha2);
    }
    md5_hex(buf, expected);
    return strcasecmp(expected, response) == 0;
}

/* Expiry of the binding: the contact's expires= parameter, else Expires. */
static long binding_expires(const struct request *req) {
    const char *contact = get_header(req, "Contact");
    const char *p = contact != NULL ? strcasestr(contact, "expires=") : NULL;
    if (p != NULL) {
        return strtol(p + 8, NULL, 10);
    }
    const char *e = get_header(req, "Expires");
    return e != NULL ? strtol(e, NULL, 10) : 3600;
}

static void append(char *out, size_t size, const char *fmt, ...) {
    size_t used = strlen(out);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out + used, size > used ? size - used : 0, fmt, ap);
    va_end(ap);
}

/* Status line and the headers every response copies from its request. */
static void response_head(char *out, size_t size, const struct request *req, const char *status) {
    out[0] = '\0';
    append(out, size, "SIP/2.0 %s\r\n", status);
    for (int i = 0; i < req->count; ++i) {
        if (strcasecmp(req->headers[i].name, "Via") == 0) {
            append(out, size, "Via: %s\r\n", req->headers[i].value);
        }
    }
    const char *to = get_header(req, "To");
    append(out, size, "From: %s\r\n", get_header(req, "From") ? get_header(req, "From") : "");
    append(out, size, "To: %s%s\r\n", to ? to : "", to && !strstr(to, "tag=") ? ";tag=aosreg" : "");
    append(out, size, "Call-ID: %s\r\n", get_header(req, "Call-ID") ? get_header(req, "Call-ID") : "");
    append(out, size, "CSeq: %s\r\n", get_header(req, "CSeq") ? get_header(req, "CSeq") : "");
    append(out, size, "Server: aos-linphone sip-registrar\r\n");
}

static void new_nonce(struct config *cfg) {
    unsigned char raw[16];
    FILE *f = fopen("/dev/urandom", "rb");
    if (f == NULL || fread(raw, 1, sizeof(raw), f) != sizeof(raw)) {
        for (size_t i = 0; i < sizeof(raw); ++i) {
            raw[i] = (unsigned char)rand();
        }
    }
    if (f != NULL) {
        fclose(f);
    }
    for (size_t i = 0; i < sizeof(raw); ++i) {
        sprintf(cfg->nonce + 2 * i, "%02x", raw[i]);
    }
}

int main(int argc, char **argv) {
    const char *address = "0.0.0.0";
    int port = 5060, timeout = 120, exit_on_cycle = 0, opt;
    struct config cfg = {"alice", "secret", "aos-test", ""};

    while ((opt = getopt(argc, argv, "a:p:u:w:r:t:x")) != -1) {
        switch (opt) {
        case 'a': address = optarg; break;
        case 'p': port = atoi(optarg); break;
        case 'u': cfg.user = optarg; break;
        case 'w': cfg.password = optarg; break;
        case 'r': cfg.realm = optarg; break;
        case 't': timeout = atoi(optarg); break;
        case 'x': exit_on_cycle = 1; break;
        default:
            fprintf(stderr, "usage: %s [-a address] [-p port] [-u user] [-w password] "
                            "[-r realm] [-t seconds] [-x]\n",
                    argv[0]);
            return 2;
        }
    }

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons((unsigned short)port);
    if (s < 0 || inet_pton(AF_INET, address, &local.sin_addr) != 1 ||
        bind(s, (struct sockaddr *)&local, sizeof(local)) != 0) {
        fprintf(stderr, "cannot listen on %s:%d: %s\n", address, port, strerror(errno));
        return 2;
    }
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    new_nonce(&cfg);
    printf("LISTENING %s:%d user %s realm %s\n", address, port, cfg.user, cfg.realm);
    fflush(stdout);

    int registered = 0, cycle_done = 0;
    time_t end = time(NULL) + timeout;
    while (!cycle_done && time(NULL) < end) {
        struct pollfd p = {s, POLLIN, 0};
        if (poll(&p, 1, 500) != 1) {
            continue;
        }
        char msg[MAX_MSG + 1], out[MAX_MSG];
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t n = recvfrom(s, msg, MAX_MSG, 0, (struct sockaddr *)&from, &flen);
        if (n <= 0) {
            continue;
        }
        msg[n] = '\0';
        struct request req;
        if (parse_request(msg, &req) != 0) {
            continue; /* a response or garbage */
        }

        if (strcmp(req.method, "REGISTER") != 0) {
            response_head(out, sizeof(out), &req,
                          strcmp(req.method, "OPTIONS") == 0 ? "200 OK" : "501 Not Implemented");
            append(out, sizeof(out), "Content-Length: 0\r\n\r\n");
            sendto(s, out, strlen(out), 0, (struct sockaddr *)&from, flen);
            continue;
        }

        const char *auth = get_header(&req, "Authorization");
        if (auth == NULL || !check_digest(&cfg, &req, auth)) {
            if (auth != NULL) {
                printf("REJECTED bad credentials\n");
            } else {
                printf("CHALLENGE\n");
            }
            response_head(out, sizeof(out), &req, "401 Unauthorized");
            append(out, sizeof(out),
                   "WWW-Authenticate: Digest realm=\"%s\", nonce=\"%s\", algorithm=MD5, qop=\"auth\"\r\n"
                   "Content-Length: 0\r\n\r\n",
                   cfg.realm, cfg.nonce);
            sendto(s, out, strlen(out), 0, (struct sockaddr *)&from, flen);
            fflush(stdout);
            continue;
        }

        const char *to = get_header(&req, "To");
        const char *contact = get_header(&req, "Contact");
        long expires = binding_expires(&req);
        response_head(out, sizeof(out), &req, "200 OK");
        if (expires > 0 && contact != NULL) {
            printf("REGISTERED %s %s %ld\n", to ? to : "?", contact, expires);
            append(out, sizeof(out), "Contact: %s\r\nExpires: %ld\r\n", contact, expires);
            registered = 1;
        } else {
            printf("UNREGISTERED %s\n", to ? to : "?");
            if (registered && exit_on_cycle) {
                cycle_done = 1;
            }
            registered = 0;
        }
        append(out, sizeof(out), "Content-Length: 0\r\n\r\n");
        sendto(s, out, strlen(out), 0, (struct sockaddr *)&from, flen);
        fflush(stdout);
    }
    close(s);
    if (exit_on_cycle) {
        printf("%s\n", cycle_done ? "RESULT registered and unregistered" : "RESULT incomplete");
        return cycle_done ? 0 : 1;
    }
    return 0;
}
