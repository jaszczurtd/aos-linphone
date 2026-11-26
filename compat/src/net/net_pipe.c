/*
 * pipe() as a connected pair of loopback TCP sockets.
 *
 * The linphone stack wakes its poll() loops from other threads by writing
 * to a pipe. A posixc pipe cannot be waited on with WaitSelect(), but a
 * socket can, and posixc read(), write(), close() and fcntl() work on socket
 * descriptors through fd.library. A TCP pair keeps the pipe's stream
 * semantics: ordered bytes, end of stream once the write end is closed.
 *
 * This definition replaces the posixc pipe() for the whole program, so it
 * sits in its own object file: a program that needs real pipes must not
 * link libaosnet's pipe.o (it is pulled in only by a reference to pipe()).
 */
#include "aos/net.h"

#include <errno.h>
#include <netinet/tcp.h>
#include <string.h>
#include <unistd.h>

/* Connections accepted from someone else before our own arrives. */
#define AOS_PIPE_MAX_STRANGERS 8

static int aos_pipe_same_addr(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_port == b->sin_port && a->sin_addr.s_addr == b->sin_addr.s_addr;
}

static void aos_pipe_close(int fd) {
    if (fd >= 0) {
        int saved = errno;
        close(fd);
        errno = saved;
    }
}

/* libnix declares pipe(int *), posixc pipe(int [2]). */
#ifdef __MORPHOS__
int pipe(int *fds) {
#else
int pipe(int fds[2]) {
#endif
    int status = aos_net_status();
    if (status != 0) {
        errno = status;
        return -1;
    }
    if (fds == NULL) {
        errno = EFAULT;
        return -1;
    }

    int listener = -1, writer = -1, reader = -1;
    struct sockaddr_in addr, writer_addr, peer;
    socklen_t len;
    int one = 1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        goto fail;
    }
    len = sizeof(addr);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(listener, 1) != 0 ||
        getsockname(listener, (struct sockaddr *)&addr, &len) != 0) {
        goto fail;
    }

    writer = socket(AF_INET, SOCK_STREAM, 0);
    if (writer < 0) {
        goto fail;
    }
    /* Completes as soon as the connection sits in the listen queue. */
    if (connect(writer, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        goto fail;
    }
    len = sizeof(writer_addr);
    if (getsockname(writer, (struct sockaddr *)&writer_addr, &len) != 0) {
        goto fail;
    }

    /* Another local program could connect to the listener first; accept
       until the connection comes from our own writer. */
    for (int strangers = 0;; ++strangers) {
        len = sizeof(peer);
        reader = accept(listener, (struct sockaddr *)&peer, &len);
        if (reader < 0) {
            goto fail;
        }
        if (aos_pipe_same_addr(&peer, &writer_addr)) {
            break;
        }
        aos_pipe_close(reader);
        reader = -1;
        if (strangers == AOS_PIPE_MAX_STRANGERS) {
            errno = EAGAIN;
            goto fail;
        }
    }
    aos_pipe_close(listener);

    /* Wake-up bytes go out at once instead of waiting for Nagle. */
    setsockopt(writer, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    fds[0] = reader;
    fds[1] = writer;
    return 0;

fail:
    aos_pipe_close(reader);
    aos_pipe_close(writer);
    aos_pipe_close(listener);
    return -1;
}
