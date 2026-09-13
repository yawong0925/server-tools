/**
 * @file main.c
 * @brief Application entry point, CLI arguments parsing, thread coordination, and output.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "types.h"
#include "discovery.h"
#include "config.h"
#include "ping_worker.h"

/**
 * @brief Displays CLI usage information and exits.
 */
static void print_usage(const char *prog_name) {
    printf("%sUsage:%s %s [OPTIONS] [config_file]\n\n", COLOR_BOLD, COLOR_RESET, prog_name);
    printf("Concurrently pings system default gateway, DNS, and configured targets.\n\n");
    printf("%sOptions:%s\n", COLOR_BOLD, COLOR_RESET);
    printf("  -c, --config <file>   Path to config file (default: %s)\n", DEFAULT_CONFIG_FILE);
    printf("  -n, --count <num>     Number of probes per host (default: %d)\n", DEFAULT_PING_COUNT);
    printf("  -h, --help            Show this help menu and exit\n\n");
}

/**
 * @brief Renders a single host result row with color coding.
 *        RED if latency exceeds 380ms, packet loss occurs, or connection fails.
 *        GREEN otherwise.
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

    /* Parse CLI Flags */
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
     * Step 1: Auto-discover Default Gateway and Upstream DNS Servers
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
     * Step 2: Parse Targets and Reference Servers from Configuration
     * ------------------------------------------------------------- */
    count = parse_config(config_file, servers, MAX_SERVERS, count, ping_count);

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
     * Step 4: Render Formatted Output Sections
     * ------------------------------------------------------------- */
    printf("%s%-20s %-20s %-16s %-10s%s\n", COLOR_BOLD, "SERVER NAME", "RESOLVED IP", "AVG PING TIME", "LOSS %", COLOR_RESET);
    for (int i = 0; i < 70; i++) putchar('-');
    putchar('\n');

    /* Section 1: Default Gateway & Discovered DNS */
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
            printf("------------------------------- Target Servers -----------------------------------\n");
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
        printf("------------------------------ Reference Servers ---------------------------------\n");
        for (int i = 0; i < count; i++) {
            if (servers[i].section == SECTION_REFERENCE) {
                print_server_row(&servers[i]);
            }
        }
    }

    return 0;
}