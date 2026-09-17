#ifndef TEAM_CACHE_LIFECYCLE_H
#define TEAM_CACHE_LIFECYCLE_H

#include <cache.h>
#include "cache_internal.h"

/* Requires zero-initialized state with no live storage. Failure resets it. */
bool cache_storage_init(cache_state* state, const cache_sim_args* args);

/* Also accepts partially initialized state or an already-destroyed state. */
void cache_storage_destroy(cache_state* state);

/* Allocate an empty invalid line on first use of this set/way. */
cache_line* cache_ensure_line(cache_state* state, size_t set_index, size_t way);

#endif
