/*
 * aos/net.h - libaosnet functions with no POSIX counterpart.
 */
#ifndef AOS_NET_H
#define AOS_NET_H

#ifdef __cplusplus
extern "C" {
#endif

/** Longest IPv4 or IPv6 address string returned by aos_net_dns_servers(),
 *  including the terminating NUL. */
#define AOS_NET_ADDRSTRLEN 46

/**
 * @brief Reports whether the network calls of libaosnet can work.
 *
 * libaosnet opens bsdsocket.library once, before main(). When that fails
 * (AROSTCP not running), the program still starts and every network call
 * fails with the errno value returned here.
 *
 * @return 0 when bsdsocket.library is open, otherwise an errno value
 *         (ENETDOWN when the library is missing).
 */
int aos_net_status(void);

/**
 * @brief Copies the DNS servers configured in AROSTCP.
 *
 * The addresses come in the order AROSTCP keeps them, as numeric strings
 * suitable for linphone_core_set_dns_servers(). AROSTCP reports IPv4
 * servers only.
 *
 * @param out  Array receiving up to @p max NUL-terminated addresses; may be
 *             NULL when @p max is 0.
 * @param max  Capacity of @p out.
 * @return Number of servers configured (it may exceed @p max; only the first
 *         @p max are copied), or -1 with errno set when the network calls
 *         are unavailable or @p max is negative.
 */
int aos_net_dns_servers(char (*out)[AOS_NET_ADDRSTRLEN], int max);

#ifdef __cplusplus
}
#endif

#endif /* AOS_NET_H */
