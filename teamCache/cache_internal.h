#ifndef TEAM_CACHE_INTERNAL_H
#define TEAM_CACHE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Host metadata only: the simulator does not store the block's data bytes. */
typedef struct cache_line {
    bool valid;
    uint64_t tag;
    /* LRU timestamp or RRIP prediction value, selected by cache policy. */
    uint64_t time_stamp;
    bool dirty; /* Modified in cache and not yet written back. */
} cache_line;

typedef enum cache_policy {
    CACHE_POLICY_LRU = 0,
    CACHE_POLICY_RRIP
} cache_policy;

typedef struct cache_state {
    /* sets[set_index] is allocated on first use; ways grow as NULL line slots. */
    cache_line*** sets;
    unsigned int s;
    size_t E;
    unsigned int b;
    size_t S;
    size_t B;

    struct cache_request* active; /* Owned; NULL when no request is pending. */
    struct cache_request* queue_head; /* Unused; split now uses a two-block cursor. */
    struct cache_request* queue_tail;
    struct cache_completion* completion; /* Cursor and count for one original request. */
    struct original_request* request_queue_head; /* Owned arrival-order FIFO. */
    struct original_request* request_queue_tail;
    struct cache_write_buffer* write_buffer; /* One background store miss. */
    uint64_t access_sequence;

    cache_policy policy;
    unsigned int rrip_bits;
    unsigned int write_buffer_mode;
} cache_state;

#endif
