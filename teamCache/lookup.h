#ifndef TEAM_CACHE_LOOKUP_H
#define TEAM_CACHE_LOOKUP_H

#include "cache_internal.h"

/* Requires live, successfully initialized storage. Returns the matching line
 * or NULL on miss. The borrowed pointer must not be freed by the caller.
 * Lookup itself does not modify state or select a line for replacement.
 */
cache_line* cache_lookup(const cache_state* state, uint64_t addr);

#endif
