#include "eviction.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void cache_eviction_complete(cache_state* state)
{
    cache_request* request = state->active;
    assert(request != NULL && request->status == REQUEST_WAITING_EVICTION);
    assert(request->target != NULL && request->target->valid);
    /* Keep the line's old identity intact until the lower transfer finishes. */
    request->target->valid = false;
}

bool cache_eviction_prepare(cache_state* state, coher* coherence)
{
    cache_request* request = state->active;
    assert(request != NULL);
    uint64_t block_number = request->op.memAddress >> state->b;
    size_t set_index = (size_t)(block_number & ((uint64_t)state->S - 1));
    cache_line* victim = state->sets[set_index][0];

    /* Placement and LRU are local to this set, even if another set is empty. */
    for (size_t way = 0; way < state->E; ++way)
    {
        cache_line* line = state->sets[set_index][way];
        if (!line->valid)
        {
            request->target = line;
            return false;
        }
        if (line->last_access < victim->last_access)
            victim = line;
    }

    request->target = victim;
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
    cache_eviction_complete(state);
    return false;
}
