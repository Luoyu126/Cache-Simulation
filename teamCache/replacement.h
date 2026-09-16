#ifndef TEAM_CACHE_REPLACEMENT_H
#define TEAM_CACHE_REPLACEMENT_H

#include "cache_internal.h"

/* Update the configured policy's metadata for a resident-line hit. */
void cache_replacement_hit(cache_state* state, cache_line* line);

/* Initialize the configured policy's metadata for a newly filled line. */
void cache_replacement_fill(cache_state* state, cache_line* line);

/*
 * Prefer the first invalid way. If the set is full, select a deterministic
 * victim according to the configured replacement policy.
 */
cache_line* cache_replacement_target(cache_state* state, size_t set_index);

#endif
