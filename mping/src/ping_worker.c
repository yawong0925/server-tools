/**
 * @file ping_worker.c
 * @brief Thread routine executing system ICMP ping and parsing latency statistics.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ping_worker.h"
#include "types.h"

void *ping_worker(void *arg) {
    ServerTarget *st = (ServerTarget *)arg;
    char cmd[MAX_LINE];
    char buffer[MAX_LINE];

    /* Assume resolution failure by default until response received */
    copy_str(st->ip, sizeof(st->ip), "Resolution Error");
    st->rtt = -1.0;
    st->packet_loss = 100.0;
    st->success = 0;

    /*
     * Build system ping command with timeout flags:
     * - macOS/BSD: -W specifies timeout in milliseconds (-W 2000 = 2s)
     * - Linux:     -W specifies timeout in seconds      (-W 2    = 2s)
     */
#if defined(__APPLE__)
    snprintf(cmd, sizeof(cmd), "ping -c %d -W 2000 %s 2>&1", st->ping_count, st->target);
#else
    snprintf(cmd, sizeof(cmd), "ping -c %d -W 2 %s 2>&1", st->ping_count, st->target);
#endif

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return NULL;
    }

    int found_ip = 0;
    double min_rtt = 0, avg_rtt = 0, max_rtt = 0, stddev_rtt = 0;

    /* Read and parse ping stdout stream */
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        /*
         * Extract resolved IP if present inside parentheses:
         * Example: "PING google.com (142.250.190.46): 56 data bytes"
         */
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

        /* Parse packet loss percentage */
        char *loss_ptr = strstr(buffer, "% packet loss");
        if (loss_ptr) {
            char *start = loss_ptr;
            while (start > buffer && (*(start - 1) == '.' || isdigit((unsigned char)*(start - 1)))) {
                start--;
            }
            st->packet_loss = atof(start);
        }

        /* Parse average round trip times from summary line */
        if (strstr(buffer, "min/avg/max") != NULL) {
            char *eq = strchr(buffer, '=');
            if (eq) {
                if (sscanf(eq + 1, "%lf/%lf/%lf/%lf", &min_rtt, &avg_rtt, &max_rtt, &stddev_rtt) >= 2) {
                    st->rtt = avg_rtt;
                    st->success = 1;
                }
            }
        }

        /* Fallback parsing for single packet ping if summary differs: "time=XX.XX ms" */
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

    /* If target was already a raw numeric IP and no parentheses were produced */
    if (!found_ip && isdigit((unsigned char)st->target[0])) {
        copy_str(st->ip, sizeof(st->ip), st->target);
    }

    return NULL;
}