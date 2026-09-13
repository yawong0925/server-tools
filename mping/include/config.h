/**
 * @file config.h
 * @brief Parser interface for loading target host configuration files.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "types.h"

/**
 * @brief Parses an INI-style configuration file containing [Target] and [Reference] blocks.
 * @param filepath Path to the configuration file on disk.
 * @param servers Array of ServerTarget structs to populate.
 * @param max_servers Maximum elements servers array can store.
 * @param current_count Number of pre-existing entries (e.g., auto-discovered servers).
 * @param ping_count Number of ping probes configured for each server.
 * @return Total count of servers in the array after parsing.
 */
int parse_config(const char *filepath, ServerTarget *servers, int max_servers, int current_count, int ping_count);

#endif /* CONFIG_H */