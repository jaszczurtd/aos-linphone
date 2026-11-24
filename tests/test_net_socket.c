/*
 * Socket calls through libaosnet, as the linphone stack makes them:
 * UDP shared between threads (RTP), socket flags and options used by
 * belle-sip and ortp, ancillary data, non-blocking TCP connect, and IPv4
 * traffic through an IPv6 socket.
 */
#include "aos_test.h"

#include "aos/net.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static struct sockaddr_in loopback_any(void)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_len = sizeof(a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return a;
}

/* A UDP socket bound to an ephemeral loopback port. */
static int udp_socket(struct sockaddr_in *addr)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    socklen_t len = sizeof(*addr);
    *addr = loopback_any();
    if (s < 0 || bind(s, (struct sockaddr *)addr, sizeof(*addr)) != 0 ||
        getsockname(s, (struct sockaddr *)addr, &len) != 0) {
        return -1;
    }
    return s;
}

/* ---- one UDP socket used by two threads --------------------------------- */

static int shared_sock = -1;
static struct sockaddr_in shared_addr, peer_addr;
static pthread_mutex_t step_lock;
static pthread_cond_t step_cond;
static int step;

static void set_step(int value)
{
    pthread_mutex_lock(&step_lock);
    step = value;
    pthread_cond_broadcast(&step_cond);
    pthread_mutex_unlock(&step_lock);
}

static void wait_step(int value)
{
    pthread_mutex_lock(&step_lock);
    while (step < value) {
        pthread_cond_wait(&step_cond, &step_lock);
    }
    pthread_mutex_unlock(&step_lock);
}

struct worker_result {
    int got_ping;
    int sent_pong;
    int wouldblock_errno;
    int closed_errno;
};

static void *udp_worker(void *arg)
{
    struct worker_result *r = arg;
    char buf[16];

    set_step(1);
    ssize_t n = recvfrom(shared_sock, buf, sizeof(buf), 0, NULL, NULL);
    r->got_ping = n == 4 && memcmp(buf, "ping", 4) == 0;
    n = sendto(shared_sock, "pong", 4, 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
    r->sent_pong = n == 4;
    errno = 0;
    n = recv(shared_sock, buf, sizeof(buf), MSG_DONTWAIT);
    r->wouldblock_errno = n < 0 ? errno : 0;
    set_step(2);

    wait_step(3);
    errno = 0;
    n = sendto(shared_sock, "late", 4, 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
    r->closed_errno = n < 0 ? errno : 0;
    return NULL;
}

static void check_shared_udp(void)
{
    struct worker_result r = {0, 0, 0, 0};
    char buf[16];
    int peer = udp_socket(&peer_addr);
    shared_sock = udp_socket(&shared_addr);
    AOS_CHECK(peer >= 0 && shared_sock >= 0, "UDP sockets bound on loopback");

    pthread_t th;
    pthread_create(&th, NULL, udp_worker, &r);
    wait_step(1);
    aos_test_sleep_ms(100); /* the worker blocks in recvfrom() */

    ssize_t n = sendto(shared_sock, "self", 4, 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
    AOS_CHECK(n == 4, "sendto() on a socket another thread is blocked on");
    n = recv(peer, buf, sizeof(buf), 0);
    AOS_CHECK(n == 4 && memcmp(buf, "self", 4) == 0, "that datagram arrives");

    sendto(peer, "ping", 4, 0, (struct sockaddr *)&shared_addr, sizeof(shared_addr));
    wait_step(2);
    AOS_CHECK(r.got_ping, "the blocked recvfrom() in the worker receives");
    AOS_CHECK(r.sent_pong, "the worker sends on the main thread's socket");
    n = recv(peer, buf, sizeof(buf), MSG_DONTWAIT);
    AOS_CHECK(n == 4 && memcmp(buf, "pong", 4) == 0, "the reply arrives");
    AOS_CHECK(r.wouldblock_errno == EWOULDBLOCK || r.wouldblock_errno == EAGAIN,
              "worker errno is EWOULDBLOCK for an empty MSG_DONTWAIT recv()");

    errno = 12345;
    AOS_CHECK(close(shared_sock) == 0, "close() of a socket in the main thread");
    set_step(3);
    pthread_join(th, NULL);
    AOS_CHECK(r.closed_errno == EBADF, "the worker then gets EBADF");
    AOS_CHECK(errno == 12345, "worker errors leave the main thread errno alone");
    close(peer);
}

/* ---- flags and options of the linphone stack ----------------------------- */

static void check_socket_flags(void)
{
    char buf[4];

    /* belle-sip's DNS resolver (dns.c) */
    int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    AOS_CHECK(s >= 0, "socket() accepts SOCK_CLOEXEC | SOCK_NONBLOCK");
    if (s >= 0) {
        errno = 0;
        ssize_t n = recv(s, buf, sizeof(buf), 0);
        AOS_CHECK(n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN),
                  "a SOCK_NONBLOCK socket does not block");
        close(s);
    }

    int t = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    AOS_CHECK(setsockopt(t, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) == 0,
              "setsockopt(SO_NOSIGPIPE) on a TCP socket (dns.c requires it)");
    AOS_CHECK(setsockopt(t, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) == 0,
              "setsockopt(TCP_NODELAY)");
    close(t);

    /* ortp's RTP sockets */
    struct sockaddr_in a = loopback_any();
    int u = socket(AF_INET, SOCK_DGRAM, 0);
    int tos = 0xb8;
    int size = 65536;
    AOS_CHECK(setsockopt(u, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == 0,
              "setsockopt(SO_REUSEADDR)");
    AOS_CHECK(setsockopt(u, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) == 0,
              "setsockopt(IP_TOS)");
    AOS_CHECK(setsockopt(u, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) == 0 &&
              setsockopt(u, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) == 0,
              "setsockopt(SO_RCVBUF, SO_SNDBUF)");
    AOS_CHECK(bind(u, (struct sockaddr *)&a, sizeof(a)) == 0, "bind() after the options");
    close(u);
}

/* ---- ancillary data (ortp receive path) ---------------------------------- */

static void check_pktinfo(void)
{
    struct sockaddr_in ra, sa;
    int r = udp_socket(&ra);
    int s = udp_socket(&sa);
    int one = 1;

    AOS_CHECK(setsockopt(r, IPPROTO_IP, IP_PKTINFO, &one, sizeof(one)) == 0,
              "setsockopt(IP_PKTINFO)");
    int ts = setsockopt(r, SOL_SOCKET, SO_TIMESTAMP, &one, sizeof(one));
    printf("info: setsockopt(SO_TIMESTAMP): %d\n", ts);

    struct iovec iov = {(void *)"rtp!", 4};
    struct msghdr out;
    memset(&out, 0, sizeof(out));
    out.msg_name = &ra;
    out.msg_namelen = sizeof(ra);
    out.msg_iov = &iov;
    out.msg_iovlen = 1;
    AOS_CHECK(sendmsg(s, &out, 0) == 4, "sendmsg() without control data");

    char data[16];
    union {
        struct cmsghdr align;
        char buf[256];
    } control;
    struct sockaddr_in from;
    struct iovec in_iov = {data, sizeof(data)};
    struct msghdr in;
    memset(&in, 0, sizeof(in));
    in.msg_name = &from;
    in.msg_namelen = sizeof(from);
    in.msg_iov = &in_iov;
    in.msg_iovlen = 1;
    in.msg_control = control.buf;
    in.msg_controllen = sizeof(control.buf);
    ssize_t n = recvmsg(r, &in, 0);
    AOS_CHECK(n == 4 && memcmp(data, "rtp!", 4) == 0, "recvmsg() receives it");
    AOS_CHECK(from.sin_port == sa.sin_port, "recvmsg() fills in the sender");

    int got_pktinfo = 0, got_timestamp = 0;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&in); c != NULL; c = CMSG_NXTHDR(&in, c)) {
        if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_PKTINFO) {
            struct in_pktinfo *pi = (struct in_pktinfo *)CMSG_DATA(c);
            got_pktinfo = pi->ipi_addr.s_addr == htonl(INADDR_LOOPBACK);
        }
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_TIMESTAMP) {
            got_timestamp = 1;
        }
    }
    AOS_CHECK(got_pktinfo, "IP_PKTINFO ancillary data names the destination address");
    printf("info: SCM_TIMESTAMP ancillary data: %s\n", got_timestamp ? "yes" : "no");
    close(r);
    close(s);
}

/* ---- non-blocking TCP connect (belle-sip stream channel) ----------------- */

static void check_tcp_connect(void)
{
    struct sockaddr_in a = loopback_any();
    socklen_t len = sizeof(a);
    int l = socket(AF_INET, SOCK_STREAM, 0);
    AOS_CHECK(l >= 0 && bind(l, (struct sockaddr *)&a, sizeof(a)) == 0 && listen(l, 64) == 0 &&
              getsockname(l, (struct sockaddr *)&a, &len) == 0,
              "TCP listener on loopback");

    int c = socket(AF_INET, SOCK_STREAM, 0);
    int fl = fcntl(c, F_GETFL);
    AOS_CHECK(fcntl(c, F_SETFL, fl | O_NONBLOCK) == 0, "fcntl(O_NONBLOCK) on a TCP socket");
    errno = 0;
    int rc = connect(c, (struct sockaddr *)&a, sizeof(a));
    printf("info: non-blocking connect(): %d, errno %d\n", rc, rc ? errno : 0);
    AOS_CHECK(rc == 0 || errno == EINPROGRESS || errno == EWOULDBLOCK,
              "non-blocking connect() succeeds or reports EINPROGRESS");

    struct pollfd p = {c, POLLOUT, 0};
    AOS_CHECK(poll(&p, 1, 2000) == 1 && (p.revents & POLLOUT), "poll() reports the connected socket writable");
    int err = -1;
    socklen_t errlen = sizeof(err);
    AOS_CHECK(getsockopt(c, SOL_SOCKET, SO_ERROR, &err, &errlen) == 0 && err == 0,
              "getsockopt(SO_ERROR) is 0 after the connect");

    int a2 = accept(l, NULL, NULL);
    AOS_CHECK(a2 >= 0, "accept() returns the connection");
    char buf[8];
    AOS_CHECK(send(c, "sip", 3, 0) == 3, "send() on the connected socket");
    AOS_CHECK(recv(a2, buf, sizeof(buf), 0) == 3 && memcmp(buf, "sip", 3) == 0,
              "recv() on the accepted socket");
    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    AOS_CHECK(getpeername(a2, (struct sockaddr *)&peer, &plen) == 0 &&
              peer.sin_addr.s_addr == htonl(INADDR_LOOPBACK),
              "getpeername() on the accepted socket");
    close(a2);
    close(c);
    close(l);
}

/* ---- IPv4 through an IPv6 socket (use_ipv6=1 in linphone) ---------------- */

static void report_dual_stack(void)
{
    int s6 = socket(AF_INET6, SOCK_DGRAM, 0);
    int zero = 0;
    struct sockaddr_in6 a6;
    socklen_t len = sizeof(a6);
    memset(&a6, 0, sizeof(a6));
    a6.sin6_len = sizeof(a6);
    a6.sin6_family = AF_INET6;
    int ok = s6 >= 0 && setsockopt(s6, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero)) == 0 &&
             bind(s6, (struct sockaddr *)&a6, sizeof(a6)) == 0 &&
             getsockname(s6, (struct sockaddr *)&a6, &len) == 0;
    printf("info: IPv6 socket bound to [::] without IPV6_V6ONLY: %s\n", ok ? "yes" : "no");
    if (!ok) {
        if (s6 >= 0) {
            close(s6);
        }
        return;
    }

    struct sockaddr_in a4;
    int s4 = udp_socket(&a4);
    struct sockaddr_in to = loopback_any();
    to.sin_port = a6.sin6_port;
    sendto(s4, "v4", 2, 0, (struct sockaddr *)&to, sizeof(to));

    struct pollfd p = {s6, POLLIN, 0};
    char buf[8];
    struct sockaddr_in6 from;
    socklen_t flen = sizeof(from);
    ssize_t n = poll(&p, 1, 1000) == 1 ? recvfrom(s6, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen) : -1;
    int mapped = n == 2 && from.sin6_family == AF_INET6 && from.sin6_addr.s6_addr[10] == 0xff &&
                 from.sin6_addr.s6_addr[11] == 0xff;
    printf("info: IPv4 datagram received on the IPv6 socket: %s\n",
           n == 2 ? (mapped ? "yes, from an IPv4-mapped address" : "yes, other address form") : "no");

    if (mapped) {
        errno = 0;
        n = sendto(s6, "v6", 2, 0, (struct sockaddr *)&from, flen);
        int send_errno = n < 0 ? errno : 0;
        struct pollfd q = {s4, POLLIN, 0};
        int back = n == 2 && poll(&q, 1, 1000) == 1 && recv(s4, buf, sizeof(buf), 0) == 2;
        printf("info: reply to the IPv4-mapped address: sendto() %d, errno %d; reaches the IPv4 "
               "socket: %s\n", (int)n, send_errno, back ? "yes" : "no");
    }
    close(s4);
    close(s6);
}

int main(void)
{
    pthread_mutex_init(&step_lock, NULL);
    pthread_cond_init(&step_cond, NULL);

    AOS_CHECK(aos_net_status() == 0, "bsdsocket.library opened before main()");
    if (aos_net_status() != 0) {
        return aos_test_result();
    }
    check_shared_udp();
    check_socket_flags();
    check_pktinfo();
    check_tcp_connect();
    report_dual_stack();
    return aos_test_result();
}
