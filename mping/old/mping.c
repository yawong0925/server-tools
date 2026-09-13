/***
 * Multi-Ping Tool for Server health
 * 
 * Compile: gcc -O2 -Wall -Wextra mping.c -o mping -pthread
 * Companion config file: mping.cfg (Server Name, Target IP/Hostname format)
 * 
 * Default Ping Count: 3 
 ***/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <unistd.h>

/* Maximum array limits and string buffer sizes */
#define MAX_SERVERS 256
#define MAX_LINE 512
#define MAX_NAME 128
#define MAX_ADDR 128
#define MAX_IP 64
#define MAX_DNS_DISCOVERED 16

/* RTT threshold: latencies above this value (in ms) trigger a red alert */
#define PING_TIMEOUT_THRESHOLD 380.0

/* Default application parameters */
#define DEFAULT_CONFIG_FILE "./mping.cfg"
#define DEFAULT_PING_COUNT 3

/* ANSI escape codes for colored terminal formatting */
#define COLOR_RED   "\033[0;31m"
#define COLOR_GREEN "\033[0;32m"
#define COLOR_RESET "\033[0m"
#define COLOR_BOLD  "\033[1m"

/* Section classification */
typedef enum {
    SECTION_DEFAULT = 0, /* Auto-discovered Gateway and DNS servers */
    SECTION_TARGET,      /* [Target] section from cfg */
    SECTION_REFERENCE    /* [Reference] section from cfg */
} SectionType;

/* Data model representing a target host and its ping results */
typedef struct {
    char name[MAX_NAME];       /* Display label (e.g., Default-Gateway, DNS-1) */
    char target[MAX_ADDR];     /* IP or domain target */
    char ip[MAX_IP];           /* Resolved IP */
    int ping_count;            /* Number of ICMP requests to transmit */
    double rtt;                /* Average round-trip time in ms (-1 on failure) */
    double packet_loss;        /* Measured packet loss percentage (0.0% - 100.0%) */
    int success;               /* Status flag: 1 = got >=1 reply, 0 = complete loss */
    SectionType section;       /* SECTION_DEFAULT, SECTION_TARGET, or SECTION_REFERENCE */
} ServerTarget;

/**
 * Copies up to (dest_size - 1) bytes and ensures null-termination.
 */
static inline void copy_str(char *dest, size_t dest_size, const char *src) {
    if (dest && dest_size > 0 && src) {
        int max_len = (int)dest_size - 1;
        snprintf(dest, dest_size, "%.*s", max_len, src);
    }
}

/**
 * Outputs CLI usage instructions.
 */
static void print_usage(const char *prog_name) {
    printf("%sUsage:%s %s [OPTIONS] [config_file]\n\n", COLOR_BOLD, COLOR_RESET, prog_name);
    printf("Concurrently pings system default gateway, all active DNS servers, and hosts configured in a file.\n\n");
    printf("%sOptions:%s\n", COLOR_BOLD, COLOR_RESET);
    printf("  -c, --config <file>   Path to configuration file (default: %s)\n", DEFAULT_CONFIG_FILE);
    printf("  -n, --count <num>     Number of ping attempts per host (default: %d)\n", DEFAULT_PING_COUNT);
    printf("  -h, --help            Show this help dialog and exit\n\n");
}

/**
 * Trims leading and trailing whitespace in-place.
 */
static void trim(char *s) {
    char *p = s;
    int l = strlen(p);
    while (l > 0 && isspace((unsigned char)p[l - 1])) p[--l] = 0;
    while (*p && isspace((unsigned char)*p)) ++p, --l;
    memmove(s, p, l + 1);
}

/**
 * Auto-detects default gateway IP using ip route (Linux) or netstat (macOS/BSD).
 */
static int get_default_gateway(char *gw_ip, size_t max_len) {
    FILE *fp = NULL;
    char buffer[256];

    /* Try Linux standard: 'ip route show default' */
    fp = popen("ip route show default 2>/dev/null", "r");
    if (fp) {
        if (fgets(buffer, sizeof(buffer), fp) != NULL) {
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

    /* Fallback for macOS/BSD: 'netstat -rn' */
    fp = popen("netstat -rn 2>/dev/null | grep -E '^default|^0.0.0.0'", "r");
    if (fp) {
        if (fgets(buffer, sizeof(buffer), fp) != NULL) {
            char dummy[64];
            char tmp[MAX_ADDR];
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

/**
 * Helper to add a DNS IP to our list if not already present (deduplication).
 */
static void add_dns_ip_if_unique(char dns_ips[][MAX_ADDR], int *count, int max_dns, const char *ip) {
    if (*count >= max_dns || !ip || strlen(ip) == 0) return;

    /* Check for duplicates */
    for (int i = 0; i < *count; i++) {
        if (strcmp(dns_ips[i], ip) == 0) {
            return;
        }
    }

    copy_str(dns_ips[*count], MAX_ADDR, ip);
    (*count)++;
}

/**
 * Discovers all active upstream and configured DNS servers.
 * Checks systemd-resolved (resolvectl), NetworkManager (nmcli), and /etc/resolv.conf.
 */
static int get_default_dns_servers(char dns_ips[][MAX_ADDR], int max_dns) {
    int count = 0;
    FILE *fp = NULL;
    char buffer[512];

    /* Method 1: resolvectl dns (Modern Ubuntu, Debian, Fedora, Arch) */
    fp = popen("resolvectl dns 2>/dev/null", "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp) && count < max_dns) {
            /* Output looks like: "Link 2 (eth0): 192.168.1.1 1.1.1.1" or "Global: 8.8.8.8" */
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

    /* Method 2: systemd-resolve --status (Older systemd releases) */
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

    /* Method 3: nmcli (NetworkManager on RHEL/CentOS/openSUSE/Debian) */
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

    /* Method 4: Parse /etc/resolv.conf (Standard POSIX / macOS fallback) */
    fp = fopen("/etc/resolv.conf", "r");
    if (fp) {
        char line[MAX_LINE];
        while (fgets(line, sizeof(line), fp) && count < max_dns) {
            trim(line);
            if (line[0] == '#' || line[0] == ';') continue;

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

    return count;
}

/**
 * Worker thread routine.
 */
void *ping_worker(void *arg) {
    ServerTarget *st = (ServerTarget *)arg;
    char cmd[512];
    char buffer[512];

    copy_str(st->ip, sizeof(st->ip), "Resolution Error");
    st->rtt = -1.0;
    st->packet_loss = 100.0;
    st->success = 0;

#if defined(__APPLE__)
    snprintf(cmd, sizeof(cmd), "ping -c %d -W 2000 %s 2>&1", st->ping_count, st->target);
#else
    snprintf(cmd, sizeof(cmd), "ping -c %d -W 2 %s 2>&1", st->ping_count, st->target);
#endif

    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;

    int found_ip = 0;
    double min_rtt = 0, avg_rtt = 0, max_rtt = 0, stddev_rtt = 0;

    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        if (!found_ip) {
            char *open_p = strchr(buffer, '(');
            char *close_p = strchr(buffer, ')');
            if (open_p && close_p && close_p > open_p) {
                int len = (int)(close_p - open_p - 1);
                if (len > 0) {
                    int max_copy = (len < (int)sizeof(st->ip) - 1) ? len : (int)sizeof(st->ip) - 1;
                    snprintf(st->ip, sizeof(st->ip), "%.*s", max_copy, open_p + 1);
                    found_ip = 1;
                }
            }
        }

        char *loss_ptr = strstr(buffer, "% packet loss");
        if (loss_ptr) {
            char *start = loss_ptr;
            while (start > buffer && (*(start - 1) == '.' || isdigit((unsigned char)*(start - 1)))) {
                start--;
            }
            st->packet_loss = atof(start);
        }

        if (strstr(buffer, "min/avg/max") != NULL) {
            char *eq = strchr(buffer, '=');
            if (eq) {
                if (sscanf(eq + 1, "%lf/%lf/%lf/%lf", &min_rtt, &avg_rtt, &max_rtt, &stddev_rtt) >= 2) {
                    st->rtt = avg_rtt;
                    st->success = 1;
                }
            }
        }

        if (st->rtt < 0) {
            char *time_ptr = strstr(buffer, "time=");
            if (time_ptr) {
                if (sscanf(time_ptr, "time=%lf", &st->rtt) == 1) {
                    st->success = 1;
                    st->packet_loss = 0.0;
                }
            }
        }
    }

    pclose(fp);

    if (!found_ip && isdigit((unsigned char)st->target[0])) {
        copy_str(st->ip, sizeof(st->ip), st->target);
    }

    return NULL;
}

/**
 * Prints a single formatted server row.
 */
static void print_server_row(const ServerTarget *st) {
    char time_str[32];
    char loss_str[16];
    const char *color;

    snprintf(loss_str, sizeof(loss_str), "%.0f%%", st->packet_loss);

    if (!st->success || st->rtt < 0) {
        copy_str(time_str, sizeof(time_str), "FAILED");
        color = COLOR_RED;
    } else if (st->packet_loss > 0.0 || st->rtt > PING_TIMEOUT_THRESHOLD) {
        snprintf(time_str, sizeof(time_str), "%.2f ms", st->rtt);
        color = COLOR_RED;
    } else {
        snprintf(time_str, sizeof(time_str), "%.2f ms", st->rtt);
        color = COLOR_GREEN;
    }

    printf("%s%-20s %-20s %-16s %-10s%s\n",
           color,
           st->name,
           st->ip,
           time_str,
           loss_str,
           COLOR_RESET);
}

int main(int argc, char *argv[]) {
    const char *config_file = DEFAULT_CONFIG_FILE;
    int ping_count = DEFAULT_PING_COUNT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                config_file = argv[++i];
            } else {
                fprintf(stderr, "Error: %s requires a file argument.\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--count") == 0) {
            if (i + 1 < argc) {
                ping_count = atoi(argv[++i]);
                if (ping_count <= 0) {
                    fprintf(stderr, "Error: Ping count must be a positive integer.\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: %s requires an integer argument.\n", argv[i]);
                return 1;
            }
        } else if (argv[i][0] != '-') {
            config_file = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    ServerTarget servers[MAX_SERVERS];
    pthread_t threads[MAX_SERVERS];
    int count = 0;

    /* -------------------------------------------------------------
     * Step 1: Auto-discover Default Gateway and All Active DNS IPs
     * ------------------------------------------------------------- */
    char gw_ip[MAX_ADDR] = {0};
    if (get_default_gateway(gw_ip, sizeof(gw_ip))) {
        copy_str(servers[count].name, sizeof(servers[count].name), "Default-Gateway");
        copy_str(servers[count].target, sizeof(servers[count].target), gw_ip);
        servers[count].ping_count = ping_count;
        servers[count].section = SECTION_DEFAULT;
        count++;
    }

    char dns_ips[MAX_DNS_DISCOVERED][MAX_ADDR];
    int num_dns = get_default_dns_servers(dns_ips, MAX_DNS_DISCOVERED);
    for (int i = 0; i < num_dns && count < MAX_SERVERS; i++) {
        snprintf(servers[count].name, sizeof(servers[count].name), "Default-DNS-%d", i + 1);
        copy_str(servers[count].target, sizeof(servers[count].target), dns_ips[i]);
        servers[count].ping_count = ping_count;
        servers[count].section = SECTION_DEFAULT;
        count++;
    }

    /* -------------------------------------------------------------
     * Step 2: Parse Target & Reference Servers from Config File
     * ------------------------------------------------------------- */
    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        fprintf(stderr, "Error opening config file '%s': ", config_file);
        perror("");
        return 1;
    }

    char line[MAX_LINE];
    SectionType current_section = SECTION_TARGET;

    while (fgets(line, sizeof(line), fp) && count < MAX_SERVERS) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        if (line[0] == '[') {
            if (strncasecmp(line, "[Target]", 8) == 0) {
                current_section = SECTION_TARGET;
            } else if (strncasecmp(line, "[Reference]", 11) == 0) {
                current_section = SECTION_REFERENCE;
            }
            continue;
        }

        char *token = strtok(line, " \t,");
        if (!token) continue;
        copy_str(servers[count].name, sizeof(servers[count].name), token);

        token = strtok(NULL, " \t,");
        if (!token) continue;
        copy_str(servers[count].target, sizeof(servers[count].target), token);

        servers[count].ping_count = ping_count;
        servers[count].section = current_section;
        count++;
    }
    fclose(fp);

    if (count == 0) {
        fprintf(stderr, "No targets or default hosts available to ping.\n");
        return 0;
    }

    printf("Pinging %d server(s) concurrently with %d ping(s) each...\n\n", count, ping_count);

    /* -------------------------------------------------------------
     * Step 3: Launch Parallel Ping Worker Threads
     * ------------------------------------------------------------- */
    for (int i = 0; i < count; i++) {
        pthread_create(&threads[i], NULL, ping_worker, &servers[i]);
    }

    for (int i = 0; i < count; i++) {
        pthread_join(threads[i], NULL);
    }

    /* -------------------------------------------------------------
     * Step 4: Display Output Divided by Sections
     * ------------------------------------------------------------- */
    printf("%s%-20s %-20s %-16s %-10s%s\n", COLOR_BOLD, "SERVER NAME", "RESOLVED IP", "AVG PING TIME", "LOSS %", COLOR_RESET);
    for (int i = 0; i < 66; i++) putchar('-');
    putchar('\n');

    /* Section 1: Default Gateway & All Discovered DNS */
    int has_default = 0;
    for (int i = 0; i < count; i++) {
        if (servers[i].section == SECTION_DEFAULT) {
            print_server_row(&servers[i]);
            has_default = 1;
        }
    }

    /* Section 2: [Target] Servers */
    int has_target = 0;
    for (int i = 0; i < count; i++) {
        if (servers[i].section == SECTION_TARGET) {
            has_target = 1;
            break;
        }
    }

    if (has_target) {
        if (has_default) {
            printf("----------------------- Target Servers ---------------------------\n");
        }
        for (int i = 0; i < count; i++) {
            if (servers[i].section == SECTION_TARGET) {
                print_server_row(&servers[i]);
            }
        }
    }

    /* Section 3: [Reference] Servers */
    int has_reference = 0;
    for (int i = 0; i < count; i++) {
        if (servers[i].section == SECTION_REFERENCE) {
            has_reference = 1;
            break;
        }
    }

    if (has_reference) {
        printf("---------------------- Reference Servers -------------------------\n");
        for (int i = 0; i < count; i++) {
            if (servers[i].section == SECTION_REFERENCE) {
                print_server_row(&servers[i]);
            }
        }
    }

    return 0;
}