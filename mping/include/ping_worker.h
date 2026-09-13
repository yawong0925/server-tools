/**
 * @file ping_worker.h
 * @brief Declarations for multi-threaded ICMP probing routines.
 */

#ifndef PING_WORKER_H
#define PING_WORKER_H

/**
 * @brief Thread entry point to ping a host and record its network metrics.
 * @param arg Pointer to a ServerTarget structure holding target parameters.
 * @return Always returns NULL.
 */
void *ping_worker(void *arg);

#endif /* PING_WORKER_H */