/**
 * @file config.c
 * @brief Configuration file parsing and section dispatch.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "config.h"

/**
 * @brief Trims leading and trailing whitespace from string in-place.
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

int parse_config(const char *filepath, ServerTarget *servers, int max_servers, int current_count, int ping_count) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        fprintf(stderr, "Error opening config file '%s': ", filepath);
        perror("");
        return current_count;
    }

    char line[MAX_LINE];
    SectionType current_section = SECTION_TARGET; /* Default section if omitted */
    int count = current_count;

    while (fgets(line, sizeof(line), fp) && count < max_servers) {
        trim(line);

        /* Skip blank lines and comments */
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        /* Parse INI-style Section Headers */
        if (line[0] == '[') {
            if (strncasecmp(line, "[Target]", 8) == 0) {
                current_section = SECTION_TARGET;
            } else if (strncasecmp(line, "[Reference]", 11) == 0) {
                current_section = SECTION_REFERENCE;
            }
            continue;
        }

        /* Field 1: Server display name */
        char *token = strtok(line, " \t,");
        if (!token) continue;
        copy_str(servers[count].name, sizeof(servers[count].name), token);

        /* Field 2: Target address (IP or Domain) */
        token = strtok(NULL, " \t,");
        if (!token) continue;
        copy_str(servers[count].target, sizeof(servers[count].target), token);

        servers[count].ping_count = ping_count;
        servers[count].section = current_section;
        count++;
    }

    fclose(fp);
    return count;
}