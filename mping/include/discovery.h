/**
 * @file discovery.h
 * @brief Function prototypes for network gateway and DNS server discovery.
 */

#ifndef DISCOVERY_H
#define DISCOVERY_H

#include <stddef.h>
#include "types.h"

/**
 * @brief Detects the primary network default gateway IPv4 address.
 * @param gw_ip Buffer to store the discovered gateway IP.
 * @param max_len Size of the destination buffer.
 * @return 1 if successfully found, 0 otherwise.
 */
int get_default_gateway(char *gw_ip, size_t max_len);

/**
 * @brief Discovers active system upstream DNS servers from systemd-resolved,
 *        NetworkManager, or /etc/resolv.conf.
 * @param dns_ips Array of buffers to populate with unique DNS IP strings.
 * @param max_dns Maximum count of DNS servers to record.
 * @return Number of unique DNS servers discovered.
 */
int get_default_dns_servers(char dns_ips[][MAX_ADDR], int max_dns);

#endif /* DISCOVERY_H */