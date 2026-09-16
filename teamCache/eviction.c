#include "eviction.h"
#include "replacement.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void cache_eviction_complete(cache_request* request)
{
    assert(request != NULL && request->status == REQUEST_WAITING_EVICTION);
    assert(request->target != NULL && request->target->valid);
    /* Keep the line's old identity intact until the lower transfer finishes. */
    request->target->valid = false;
}

bool cache_eviction_prepare(cache_state* state, coher* coherence,
                            cache_request* request)
{
    assert(request != NULL);
    uint64_t block_number = request->op.memAddress >> state->b;
    size_t set_index = (size_t)(block_number & ((uint64_t)state->S - 1));
    cache_line* victim = cache_replacement_target(state, set_index);
    request->target = victim;
    if (!victim->valid)
        return false;

    request->victim_address = ((victim->tag << state->s) | (uint64_t)set_index)
                              << state->b;
    request->status = REQUEST_WAITING_EVICTION;
    if (coherence == NULL || coherence->invlReq == NULL)
    {
        fprintf(stderr, "teamCache: missing coherence eviction interface\n");
        exit(EXIT_FAILURE);
    }
    if (coherence->invlReq(request->victim_address, request->processor))
        return true;

    /* Zero means already complete, so no later notification is required. */
    cache_eviction_complete(request);
    return false;
}
