/**
 * @file types.h
 * @brief Common definitions, data structures, and utility macros.
 */

#ifndef TYPES_H
#define TYPES_H

#include <stdio.h>
#include <stddef.h>

/* Maximum capacity limits for buffers and arrays */
#define MAX_SERVERS          256  /* Maximum number of hosts to monitor concurrently */
#define MAX_LINE             512  /* Maximum line length when reading files or shell streams */
#define MAX_NAME             128  /* Maximum length for a host display name */
#define MAX_ADDR             128  /* Maximum length for a host target address/hostname */
#define MAX_IP               64   /* Maximum length for resolved IP string representation */
#define MAX_DNS_DISCOVERED   16   /* Maximum auto-discovered DNS resolvers */

/* Latency threshold in milliseconds (values above trigger red warning alert) */
#define PING_TIMEOUT_THRESHOLD 380.0

/* Default application values */
#define DEFAULT_CONFIG_FILE  "./mping.cfg"
#define DEFAULT_PING_COUNT   3

/* ANSI escape codes for formatted and colored terminal output */
#define COLOR_RED     "\033[0;31m"
#define COLOR_GREEN   "\033[0;32m"
#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"

/**
 * @enum SectionType
 * @brief Categorization of target entries for visual sectioning in output.
 */
typedef enum {
    SECTION_DEFAULT = 0, /* Auto-discovered Gateway and DNS servers */
    SECTION_TARGET,      /* Monitored targets from [Target] config section */
    SECTION_REFERENCE    /* Baseline reference hosts from [Reference] config section */
} SectionType;

/**
 * @struct ServerTarget
 * @brief Encapsulates host details and measured performance metrics.
 */
typedef struct {
    char name[MAX_NAME];       /* Display label */
    char target[MAX_ADDR];     /* User-supplied address or hostname */
    char ip[MAX_IP];           /* Resolved numeric IP extracted from ping */
    int ping_count;            /* Number of ICMP probes to send */
    double rtt;                /* Average round-trip time in ms (-1.0 on failure) */
    double packet_loss;        /* Measured packet loss percentage (0.0 to 100.0) */
    int success;               /* 1 if at least one reply received; 0 otherwise */
    SectionType section;       /* Belongs to SECTION_DEFAULT, SECTION_TARGET, or SECTION_REFERENCE */
} ServerTarget;

/**
 * @brief Bounded string copy helper that guarantees null-termination and prevents
 *        GCC string truncation compiler warnings under optimization flags.
 * @param dest Destination buffer.
 * @param dest_size Size of destination buffer in bytes.
 * @param src Source string to copy.
 */
static inline void copy_str(char *dest, size_t dest_size, const char *src) {
    if (dest && dest_size > 0 && src) {
        int max_len = (int)dest_size - 1;
        snprintf(dest, dest_size, "%.*s", max_len, src);
    }
}

#endif /* TYPES_H */