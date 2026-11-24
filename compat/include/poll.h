/*
 * poll.h - poll() for AROS, implemented by libaosnet over WaitSelect().
 *
 * Only socket descriptors can be waited on. Any other descriptor is
 * reported as POLLNVAL. End of stream and socket errors show up as POLLIN
 * (or POLLOUT), as with select(); the following read or write returns 0 or
 * the error.
 */
#ifndef AOS_POLL_H
#define AOS_POLL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int nfds_t;

struct pollfd {
    int fd;         /* descriptor; negative entries are skipped */
    short events;   /* requested events */
    short revents;  /* returned events */
};

#define POLLIN      0x0001
#define POLLPRI     0x0002
#define POLLOUT     0x0004
#define POLLERR     0x0008
#define POLLHUP     0x0010
#define POLLNVAL    0x0020
#define POLLRDNORM  0x0040
#define POLLRDBAND  0x0080
#define POLLWRNORM  0x0100
#define POLLWRBAND  0x0200

/**
 * @brief Waits until one of the sockets in @p fds is ready.
 *
 * @param fds      Descriptors and requested events; entries with a negative
 *                 fd get revents 0.
 * @param nfds     Number of entries in @p fds.
 * @param timeout  Milliseconds to wait; 0 returns at once, a negative value
 *                 waits without a limit.
 * @return Number of entries with non-zero revents, 0 on timeout, or -1 with
 *         errno (EINTR on a break signal, ENOMEM, EINVAL, ENETDOWN).
 */
int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#ifdef __cplusplus
}
#endif

#endif /* AOS_POLL_H */
