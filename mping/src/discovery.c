/**
 * @file discovery.c
 * @brief Automated local network environment discovery routines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "discovery.h"

/**
 * @brief Strips leading and trailing whitespace characters in-place.
 */
static void trim(char *s) {
    char *p = s;
    int l = (int)strlen(p);
    while (l > 0 && isspace((unsigned char)p[l - 1])) {
        p[--l] = '\0';
    }
    while (*p && isspace((unsigned char)*p)) {
        ++p;
        --l;
    }
    memmove(s, p, l + 1);
}

/**
 * @brief Appends an IP address to the DNS discovery list if not already present.
 */
static void add_dns_ip_if_unique(char dns_ips[][MAX_ADDR], int *count, int max_dns, const char *ip) {
    if (*count >= max_dns || !ip || strlen(ip) == 0) {
        return;
    }

    /* Verify uniqueness against already discovered addresses */
    for (int i = 0; i < *count; i++) {
        if (strcmp(dns_ips[i], ip) == 0) {
            return;
        }
    }

    copy_str(dns_ips[*count], MAX_ADDR, ip);
    (*count)++;
}

int get_default_gateway(char *gw_ip, size_t max_len) {
    FILE *fp = NULL;
    char buffer[MAX_LINE];

    /* Attempt 1: Linux iproute2 command */
    fp = popen("ip route show default 2>/dev/null", "r");
    if (fp) {
        if (fgets(buffer, sizeof(buffer), fp) != NULL) {
            /* Typical output: "default via 192.168.1.1 dev eth0 ..." */
            char *via = strstr(buffer, "via ");
            if (via) {
                char tmp[MAX_ADDR];
                if (sscanf(via + 4, "%127s", tmp) == 1) {
                    copy_str(gw_ip, max_len, tmp);
                    pclose(fp);
                    return 1;
                }
            }
        }
        pclose(fp);
    }

    /* Attempt 2: BSD / macOS routing table fallback */
    fp = popen("netstat -rn 2>/dev/null | grep -E '^default|^0.0.0.0'", "r");
    if (fp) {
        if (fgets(buffer, sizeof(buffer), fp) != NULL) {
            /* Typical output: "default 192.168.1.1 UGSc ..." */
            char dummy[64], tmp[MAX_ADDR];
            if (sscanf(buffer, "%63s %127s", dummy, tmp) == 2) {
                copy_str(gw_ip, max_len, tmp);
                pclose(fp);
                return 1;
            }
        }
        pclose(fp);
    }

    return 0;
}

int get_default_dns_servers(char dns_ips[][MAX_ADDR], int max_dns) {
    int count = 0;
    FILE *fp = NULL;
    char buffer[MAX_LINE];

    /* Method 1: resolvectl (Standard on modern Ubuntu, Debian, Fedora, Arch) */
    fp = popen("resolvectl dns 2>/dev/null", "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp) && count < max_dns) {
            /* Example line: "Link 2 (eth0): 192.168.1.1 1.1.1.1" */
            char *colon = strchr(buffer, ':');
            if (colon) {
                char *token = strtok(colon + 1, " \t\r\n");
                while (token && count < max_dns) {
                    add_dns_ip_if_unique(dns_ips, &count, max_dns, token);
                    token = strtok(NULL, " \t\r\n");
                }
            }
        }
        pclose(fp);
    }

    /* Method 2: systemd-resolve fallback (Older systemd releases) */
    if (count == 0) {
        fp = popen("systemd-resolve --status 2>/dev/null | grep 'DNS Servers:'", "r");
        if (fp) {
            while (fgets(buffer, sizeof(buffer), fp) && count < max_dns) {
                char *colon = strchr(buffer, ':');
                if (colon) {
                    char *token = strtok(colon + 1, " \t\r\n");
                    while (token && count < max_dns) {
                        add_dns_ip_if_unique(dns_ips, &count, max_dns, token);
                        token = strtok(NULL, " \t\r\n");
                    }
                }
            }
            pclose(fp);
        }
    }

    /* Method 3: nmcli (NetworkManager on RHEL, CentOS, Rocky Linux) */
    if (count == 0) {
        fp = popen("nmcli dev show 2>/dev/null | grep 'IP4.DNS'", "r");
        if (fp) {
            while (fgets(buffer, sizeof(buffer), fp) && count < max_dns) {
                char *colon = strchr(buffer, ':');
                if (colon) {
                    char ip_buf[MAX_ADDR];
                    if (sscanf(colon + 1, "%127s", ip_buf) == 1) {
                        add_dns_ip_if_unique(dns_ips, &count, max_dns, ip_buf);
                    }
                }
            }
            pclose(fp);
        }
    }

    /* Method 4: POSIX /etc/resolv.conf inspection */
    if (count == 0) {
        fp = fopen("/etc/resolv.conf", "r");
        if (fp) {
            char line[MAX_LINE];
            while (fgets(line, sizeof(line), fp) && count < max_dns) {
                trim(line);
                if (line[0] == '#' || line[0] == ';') {
                    continue;
                }
                if (strncmp(line, "nameserver", 10) == 0) {
                    char *p = line + 10;
                    while (*p && isspace((unsigned char)*p)) p++;
                    if (*p) {
                        char ip_buf[MAX_ADDR];
                        if (sscanf(p, "%127s", ip_buf) == 1) {
                            add_dns_ip_if_unique(dns_ips, &count, max_dns, ip_buf);
                        }
                    }
                }
            }
            fclose(fp);
        }
    }

    return count;
}