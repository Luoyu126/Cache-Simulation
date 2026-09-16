#ifndef TEAM_CACHE_WRITE_BUFFER_H
#define TEAM_CACHE_WRITE_BUFFER_H

#include "access.h"

typedef struct cache_write_buffer {
    cache_request* request; /* Owned until background completion or destroy. */
    bool processor_notified;
    bool data_complete;
} cache_write_buffer;

/* Eligible only for a one-cache-line store miss in mode 1. */
bool cache_write_buffer_eligible(const cache_state* state,
                                 const cache_request* request);

/* Transfer state->active into the empty buffer. NULL means success. */
const char* cache_write_buffer_adopt(cache_state* state);

/* Free the buffered request and slot, if occupied. */
void cache_write_buffer_destroy(cache_state* state);

#endif
