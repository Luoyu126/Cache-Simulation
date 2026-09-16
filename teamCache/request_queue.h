#ifndef TEAM_CACHE_REQUEST_QUEUE_H
#define TEAM_CACHE_REQUEST_QUEUE_H

#include <cache.h>
#include "cache_internal.h"

typedef struct original_request {
    trace_op op;
    int processor;
    int64_t request_tag;
    void (*callback)(int, int64_t);
    struct original_request* next;
} original_request;

/* Copy one processor request onto the arrival-order FIFO. NULL means success. */
const char* cache_request_enqueue(cache_state* state, const trace_op* op,
                                  int processor, int64_t tag,
                                  void (*callback)(int, int64_t));

/* Remove and return the FIFO head. The caller owns the returned request. */
original_request* cache_request_take(cache_state* state);

/* Release every original request that has not started. */
void cache_request_queue_destroy(cache_state* state);

#endif
