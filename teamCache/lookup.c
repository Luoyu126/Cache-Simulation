#include "lookup.h"

#include <assert.h>

cache_line* cache_lookup(const cache_state* state, uint64_t addr)
{
    assert(state != NULL && state->sets != NULL);

    /* Phase 01 validates s/b. Separate shifts avoid shifting by s+b == 64. */
    uint64_t block_number = addr >> state->b;
    size_t set_index = (size_t)(block_number & ((uint64_t)state->S - 1));
    uint64_t tag = block_number >> state->s;

    for (size_t way = 0; way < state->E; ++way)
    {
        cache_line* line = state->sets[set_index][way];
        if (line->valid && line->tag == tag)
            return line;
    }
    return NULL;
}
