#ifndef TEAM_CACHE_ACCESS_H
#define TEAM_CACHE_ACCESS_H

#include <cache.h>
#include "cache_internal.h"

typedef enum request_status {
    REQUEST_QUEUED,
    REQUEST_WAITING_BUFFER,
    REQUEST_WAITING_EVICTION,
    REQUEST_WAITING_SPLIT_EVICTION,
    REQUEST_WAITING_DATA,
    REQUEST_READY
} request_status;

typedef struct cache_request {
    trace_op op; /* Independent value copy, owned with this allocation. */
    int processor;
    int64_t request_tag;
    void (*callback)(int, int64_t);
    request_status status;
    cache_line* target; /* Borrowed from sets; never freed with the request. */
    uint64_t victim_address;
    uint64_t block_index; /* Ordinal within the original processor request. */
    struct cache_request* next; /* Owned FIFO linkage; NULL while active. */
} cache_request;

void cache_access_request(cache_state* state, coher* coherence,
                          const trace_op* op, int processor, int64_t tag,
                          void (*callback)(int, int64_t));
void cache_access_event(cache_state* state, coher* coherence, int type, int processor,
                        uint64_t address);
void cache_access_tick(cache_state* state, coher* coherence);
void cache_access_destroy(cache_state* state);

#endif
