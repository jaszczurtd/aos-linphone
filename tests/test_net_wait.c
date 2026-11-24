/*
 * poll() and select() over WaitSelect(), pipe() as a socket pair, and the
 * two patterns the linphone stack depends on: belle-sip's loop woken through
 * its control pipe from another thread, and two threads sending and
 * receiving UDP at the same time.
 */
#include "aos_test.h"

#include "aos/net.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

static int udp_socket(struct sockaddr_in *addr) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    socklen_t len = sizeof(*addr);
    memset(addr, 0, sizeof(*addr));
    addr->sin_len = sizeof(*addr);
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (s < 0 || bind(s, (struct sockaddr *)addr, sizeof(*addr)) != 0 ||
        getsockname(s, (struct sockaddr *)addr, &len) != 0) {
        return -1;
    }
    return s;
}

static void check_poll_basics(void) {
    struct sockaddr_in ra, sa;
    int r = udp_socket(&ra);
    int s = udp_socket(&sa);
    AOS_CHECK(r >= 0 && s >= 0, "two UDP sockets");

    long t0 = aos_test_now_ms();
    AOS_CHECK(poll(NULL, 0, 50) == 0, "poll() with no descriptors sleeps and returns 0");
    long slept = aos_test_now_ms() - t0;
    printf("info: poll(NULL, 0, 50) took %ld ms\n", slept);
    AOS_CHECK(slept >= 40 && slept < 1000, "and waits about the timeout");

    struct pollfd p[2] = {{r, POLLIN | POLLERR, 0}, {-1, POLLIN, 0}};
    t0 = aos_test_now_ms();
    AOS_CHECK(poll(p, 2, 100) == 0 && p[0].revents == 0, "poll() on an idle socket times out");
    long waited = aos_test_now_ms() - t0;
    printf("info: poll() timeout of 100 ms took %ld ms\n", waited);
    AOS_CHECK(waited >= 90 && waited < 1000, "the timeout lasts about 100 ms");

    sendto(s, "x", 1, 0, (struct sockaddr *)&ra, sizeof(ra));
    p[1].revents = 77;
    AOS_CHECK(poll(p, 2, 1000) == 1, "poll() reports one ready descriptor");
    AOS_CHECK(p[0].revents == POLLIN, "revents is exactly POLLIN, as belle-sip expects");
    AOS_CHECK(p[1].revents == 0, "an entry with a negative fd gets revents 0");

    struct pollfd w = {s, POLLOUT, 0};
    AOS_CHECK(poll(&w, 1, 0) == 1 && w.revents == POLLOUT, "a UDP socket is writable");

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(r, &rfds);
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    AOS_CHECK(select(r + 1, &rfds, NULL, NULL, &tv) == 1 && FD_ISSET(r, &rfds),
              "select() reports the same socket readable");

    /* A posixc file next to a ready socket. */
    int file = open("RAM:aos-poll-test", O_CREAT | O_RDWR | O_TRUNC, 0644);
    AOS_CHECK(file > 2, "a posixc file descriptor for the POLLNVAL case");
    struct pollfd mixed[2] = {{file, POLLIN, 0}, {r, POLLIN, 0}};
    int rc = poll(mixed, 2, 1000);
    printf("info: poll(file, socket): %d, revents 0x%x 0x%x\n", rc, mixed[0].revents,
           mixed[1].revents);
    AOS_CHECK(rc == 2 && mixed[0].revents == POLLNVAL && mixed[1].revents == POLLIN,
              "a non-socket is POLLNVAL and the ready socket is still reported");
    /* A non-socket above the base's descriptor table, which WaitSelect()
       would ignore silently: poll() must still report it at once. */
    const int high = 1000;
    errno = 0;
    if (fcntl(high, F_GETFD) == -1 && errno == EBADF && dup2(file, high) == high) {
        struct pollfd lone = {high, POLLIN, 0};
        t0 = aos_test_now_ms();
        rc = poll(&lone, 1, 2000);
        long took = aos_test_now_ms() - t0;
        printf("info: poll(fd %d, a file) returned %d after %ld ms\n", high, rc, took);
        AOS_CHECK(rc == 1 && lone.revents == POLLNVAL && took < 1000,
                  "a high-numbered non-socket is POLLNVAL without waiting");
        close(high);
    } else {
        AOS_CHECK(0, "descriptor 1000 free for the POLLNVAL case");
    }
    close(file);
    unlink("RAM:aos-poll-test");

    char buf[4];
    recv(r, buf, sizeof(buf), 0);
    close(r);
    close(s);
}

/* ---- belle-sip main loop woken through its control pipe ------------------ */

static int control_fds[2];

static void *waker(void *arg) {
    (void)arg;
    aos_test_sleep_ms(100);
    /* belle_sip_main_loop_wake_up(): blocking write end */
    write(control_fds[1], "wake up!", 1);
    return NULL;
}

/* belle-sip's clear_pipe(): read until EAGAIN */
static ssize_t clear_pipe(int fd) {
    char buffer[1024];
    ssize_t nread, total = 0;
    do {
        nread = read(fd, buffer, sizeof(buffer));
        if (nread > 0) {
            total += nread;
        }
    } while (nread > 0);
    return nread < 0 && errno != EAGAIN && errno != EWOULDBLOCK ? -1 : total;
}

static void check_pipe_wakeup(void) {
    AOS_CHECK(pipe(control_fds) == 0, "pipe()");
    printf("info: pipe descriptors %d and %d\n", control_fds[0], control_fds[1]);
    AOS_CHECK(fcntl(control_fds[0], F_SETFL, O_NONBLOCK) == 0, "O_NONBLOCK on the read end");

    struct sockaddr_in a;
    int sip = udp_socket(&a);
    struct pollfd pfd[2] = {{sip, POLLIN, 0}, {control_fds[0], POLLIN, 0}};

    pthread_t th;
    pthread_create(&th, NULL, waker, NULL);
    long t0 = aos_test_now_ms();
    int rc = poll(pfd, 2, -1);
    long waited = aos_test_now_ms() - t0;
    pthread_join(th, NULL);
    printf("info: loop woke after %ld ms\n", waited);
    AOS_CHECK(rc == 1 && pfd[1].revents == POLLIN && pfd[0].revents == 0,
              "poll() without a timeout wakes when another thread writes the pipe");
    AOS_CHECK(waited >= 90 && waited < 2000, "it wakes when the write happens");
    AOS_CHECK(clear_pipe(control_fds[0]) == 1, "clear_pipe() reads the wake-up byte and stops at EAGAIN");

    AOS_CHECK(write(control_fds[1], "abc", 3) == 3, "the write end takes more bytes");
    char buf[8];
    struct pollfd rd = {control_fds[0], POLLIN, 0};
    poll(&rd, 1, 1000);
    AOS_CHECK(read(control_fds[0], buf, sizeof(buf)) == 3 && memcmp(buf, "abc", 3) == 0,
              "bytes come out in order");

    close(control_fds[1]);
    rd.revents = 0;
    poll(&rd, 1, 1000);
    AOS_CHECK(read(control_fds[0], buf, sizeof(buf)) == 0, "end of stream after the write end is closed");
    close(control_fds[0]);
    close(sip);
}

/* ---- two threads sending and receiving at once ---------------------------- */

#define STREAM_PACKETS 300
#define STREAM_SIZE    172 /* an RTP packet with 160 bytes of G.711 */

struct stream {
    int sock;
    struct sockaddr_in peer;
    int received;
    int in_order;
    int send_errors;
};

static void *run_stream(void *arg) {
    struct stream *st = arg;
    unsigned char packet[STREAM_SIZE];
    int expected = 0;
    long deadline = 0;

    st->in_order = 1;
    for (int sent = 0; sent < STREAM_PACKETS || st->received < STREAM_PACKETS;) {
        if (sent < STREAM_PACKETS) {
            memset(packet, 0, sizeof(packet));
            memcpy(packet, &sent, sizeof(sent));
            if (sendto(st->sock, packet, sizeof(packet), 0, (struct sockaddr *)&st->peer,
                       sizeof(st->peer)) != (ssize_t)sizeof(packet)) {
                ++st->send_errors;
            }
            if (++sent == STREAM_PACKETS) {
                deadline = aos_test_now_ms() + 2000;
            }
        } else if (aos_test_now_ms() > deadline) {
            break;
        }
        struct pollfd p = {st->sock, POLLIN, 0};
        while (poll(&p, 1, sent < STREAM_PACKETS ? 0 : 50) == 1) {
            int seq;
            if (recv(st->sock, packet, sizeof(packet), 0) == (ssize_t)sizeof(packet)) {
                memcpy(&seq, packet, sizeof(seq));
                if (seq != expected) {
                    st->in_order = 0;
                }
                expected = seq + 1;
                ++st->received;
            }
            p.revents = 0;
        }
        if (sent < STREAM_PACKETS) {
            aos_test_sleep_ms(2);
        }
    }
    return NULL;
}

static void check_two_streams(void) {
    struct stream a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.sock = udp_socket(&b.peer);
    b.sock = udp_socket(&a.peer);

    pthread_t th;
    long t0 = aos_test_now_ms();
    pthread_create(&th, NULL, run_stream, &a);
    run_stream(&b);
    pthread_join(th, NULL);
    printf("info: two streams of %d packets: %d and %d received in %ld ms\n", STREAM_PACKETS,
           a.received, b.received, aos_test_now_ms() - t0);
    AOS_CHECK(a.send_errors == 0 && b.send_errors == 0, "no send errors in either thread");
    AOS_CHECK(a.received == STREAM_PACKETS && b.received == STREAM_PACKETS,
              "both threads receive every packet while sending");
    AOS_CHECK(a.in_order && b.in_order, "packets arrive in order");
    close(a.sock);
    close(b.sock);
}

int main(void) {
    AOS_CHECK(aos_net_status() == 0, "bsdsocket.library opened before main()");
    if (aos_net_status() != 0) {
        return aos_test_result();
    }
    check_poll_basics();
    check_pipe_wakeup();
    check_two_streams();
    return aos_test_result();
}
