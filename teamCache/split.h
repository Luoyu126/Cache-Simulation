#ifndef TEAM_CACHE_SPLIT_H
#define TEAM_CACHE_SPLIT_H

#include "access.h"

/* One keyed entry: Phase 05 accepts only one original processor request. */
typedef struct cache_completion {
    int processor;
    int64_t request_tag;
    uint64_t total;
    uint64_t completed;
} cache_completion;

/* Build all parts before starting any access. NULL means success. */
const char* cache_split_prepare(cache_state* state, const trace_op* op,
                                int processor, int64_t tag,
                                void (*callback)(int, int64_t));
cache_request* cache_split_take(cache_state* state);
/* Count one retired block; release the count entry if the original is done. */
bool cache_split_complete(cache_state* state, const cache_request* request);
/* Free queued parts and the count entry; active is owned by access.c. */
void cache_split_destroy(cache_state* state);

#endif
