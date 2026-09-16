#ifndef TEAM_CACHE_EVICTION_H
#define TEAM_CACHE_EVICTION_H

#include "access.h"

/* Select within the incoming block's set. True means eviction is pending. */
bool cache_eviction_prepare(cache_state* state, coher* coherence,
                            cache_request* request);

/* Called only when the selected victim's eviction has completed. */
void cache_eviction_complete(cache_request* request);

#endif
