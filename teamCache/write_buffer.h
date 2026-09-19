#ifndef TEAM_CACHE_WRITE_BUFFER_H
#define TEAM_CACHE_WRITE_BUFFER_H

#include "access.h"
#include "split.h"

/*
 * One buffered store. Entries form a FIFO; only the head is ever "started"
 * (has an eviction/fetch outstanding with the coherence component), since
 * the cache may only have one outstanding memory request at a time. Later
 * entries sit queued until the head retires and frees that single slot.
 */
typedef struct cache_write_buffer_entry {
    uint64_t block_address;
    cache_request* request;  /* Owned until this entry retires. */
    cache_completion* completion; /* Owned; this store's own dispatch-completion,
                                    * detached from state->completion at push
                                    * time so the foreground pipeline is free to
                                    * start the next queued request right away
                                    * instead of waiting for this entry's later
                                    * processor notification (matches refCache's
                                    * immediate write-buffer "detach" behavior,
                                    * confirmed via disassembly). */
    bool started;            /* Eviction/fetch has been issued for this entry. */
    bool processor_notified; /* The owning access already returned to the processor. */
    bool data_complete;      /* The background fetch has filled the target line. */
    struct cache_write_buffer_entry* next;
} cache_write_buffer_entry;

/* FIFO of buffered stores; cache_state holds one of these once non-empty. */
typedef struct cache_write_buffer {
    cache_write_buffer_entry* head; /* In-flight, or next to start. */
    cache_write_buffer_entry* tail;
    unsigned int count;
} cache_write_buffer;

/* Capacity implied by -w: 0 none, modes 1-2 a single slot, 3+ queues W writes. */
unsigned int cache_write_buffer_capacity(const cache_state* state);

/* True when request is a single-block store that still fits in the buffer.
 * An unaligned (multi-block) access is never eligible, per spec. */
bool cache_write_buffer_eligible(const cache_state* state,
                                 const cache_request* request);

/* An entry still filling this block, if any. Only modes 2+ report a match:
 * that lets a later read be served from it and a later write coalesce into
 * it instead of missing again. */
cache_write_buffer_entry* cache_write_buffer_find(const cache_state* state,
                                                  uint64_t block_address);

/* Move state->active into a new tail entry (does not start the coherence
 * handshake). Returns NULL only on allocation failure. */
cache_write_buffer_entry* cache_write_buffer_push(cache_state* state,
                                                  uint64_t block_address);

/* Free the head entry and pop it off. Returns the new head, or NULL if the
 * buffer is now empty. */
cache_write_buffer_entry* cache_write_buffer_pop_head(cache_state* state);

/* Free every buffered entry and the buffer itself. */
void cache_write_buffer_destroy(cache_state* state);

#endif
