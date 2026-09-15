#ifndef TEAM_CACHE_INTERNAL_H
#define TEAM_CACHE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Host metadata only: the simulator does not store the block's data bytes. */
typedef struct cache_line {
    bool valid;
    uint64_t tag;
    uint64_t last_access;
    bool dirty; /* Initially false; access-time rules are a later-phase TODO. */
} cache_line;

typedef enum cache_policy {
    CACHE_POLICY_LRU = 0,
    CACHE_POLICY_RRIP
} cache_policy;

typedef struct cache_state {
    /* sets[set_index][way] points to an individually allocated cache_line. */
    cache_line*** sets;
    unsigned int s;
    size_t E;
    unsigned int b;
    size_t S;
    size_t B;

    /* Configuration only until the corresponding phases are implemented. */
    cache_policy policy;
    unsigned int rrip_bits;
    unsigned int write_buffer_mode;
} cache_state;

#endif
