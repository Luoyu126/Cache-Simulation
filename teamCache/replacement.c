#include "replacement.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void fail(const char* message)
{
    fprintf(stderr, "teamCache: %s\n", message);
    exit(EXIT_FAILURE);
}

static uint64_t rrip_max(const cache_state* state)
{
    assert(state->rrip_bits >= 1 && state->rrip_bits <= 64);
    if (state->rrip_bits == 64)
        return UINT64_MAX;
    return (UINT64_C(1) << state->rrip_bits) - 1;
}

static void lru_update(cache_state* state, cache_line* line)
{
    if (state->access_sequence == UINT64_MAX)
        fail("access sequence exhausted");
    line->time_stamp = ++state->access_sequence;
}

static cache_line* lru_victim(cache_state* state, size_t set_index)
{
    cache_line* victim = state->sets[set_index][0];
    for (size_t way = 1; way < state->E; ++way)
    {
        cache_line* line = state->sets[set_index][way];
        if (line->time_stamp < victim->time_stamp)
            victim = line;
    }
    return victim;
}

static cache_line* rrip_victim(cache_state* state, size_t set_index)
{
    uint64_t maximum = rrip_max(state);
    for (;;)
    {
        /* Ties at the max RRPV break toward the highest way index (matches
         * the reference simulator's eviction order, confirmed empirically). */
        for (size_t way = state->E; way-- > 0; )
        {
            cache_line* line = state->sets[set_index][way];
            if (line->time_stamp == maximum)
                return line;
        }

        /*
         * The preceding scan proves every value is below maximum, so this
         * increment cannot wrap even when k is 64.
         */
        for (size_t way = 0; way < state->E; ++way)
            ++state->sets[set_index][way]->time_stamp;
    }
}

void cache_replacement_hit(cache_state* state, cache_line* line)
{
    assert(state != NULL && line != NULL);
    if (state->policy == CACHE_POLICY_LRU)
        lru_update(state, line);
    else
        line->time_stamp = 0;
}

void cache_replacement_fill(cache_state* state, cache_line* line)
{
    assert(state != NULL && line != NULL);
    if (state->policy == CACHE_POLICY_LRU)
        lru_update(state, line);
    else
        line->time_stamp = rrip_max(state) - 1;
}

cache_line* cache_replacement_target(cache_state* state, size_t set_index)
{
    assert(state != NULL && set_index < state->S && state->E > 0);
    for (size_t way = 0; way < state->E; ++way)
    {
        cache_line* line = state->sets[set_index][way];
        if (!line->valid)
            return line;
    }

    if (state->policy == CACHE_POLICY_LRU)
        return lru_victim(state, set_index);
    return rrip_victim(state, set_index);
}
