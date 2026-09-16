#ifndef TEAM_CACHE_LIFECYCLE_H
#define TEAM_CACHE_LIFECYCLE_H

#include <cache.h>
#include "cache_internal.h"

/* Requires zero-initialized state with no live storage. Failure resets it. */
bool cache_storage_init(cache_state* state, const cache_sim_args* args);

/* Also accepts partially initialized state or an already-destroyed state. */
void cache_storage_destroy(cache_state* state);

#endif
