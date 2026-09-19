#include "write_buffer.h"
#include "split.h"

#include <assert.h>
#include <stdlib.h>

unsigned int cache_write_buffer_capacity(const cache_state* state)
{
    unsigned int mode = state->write_buffer_mode;
    if (mode == 0)
        return 0;
    if (mode <= 2)
        return 1; /* Modes 1-2 buffer a single background store. */
    return mode;  /* Mode 3+: -w's value is the queue depth itself. */
}

bool cache_write_buffer_eligible(const cache_state* state,
                                 const cache_request* request)
{
    assert(state != NULL && request != NULL);
    assert(state->completion != NULL);
    unsigned int count = state->write_buffer == NULL ? 0
                                                      : state->write_buffer->count;
    return request->op.op == MEM_STORE
        && state->completion->total == 1 /* Unaligned accesses never buffer. */
        && count < cache_write_buffer_capacity(state);
}

cache_write_buffer_entry* cache_write_buffer_find(const cache_state* state,
                                                  uint64_t block_address)
{
    if (state->write_buffer_mode < 2 || state->write_buffer == NULL)
        return NULL; /* Reading from / coalescing into the buffer needs mode 2+. */
    for (cache_write_buffer_entry* entry = state->write_buffer->head;
         entry != NULL; entry = entry->next)
        if (entry->block_address == block_address && !entry->data_complete)
            return entry;
    return NULL;
}

cache_write_buffer_entry* cache_write_buffer_push(cache_state* state,
                                                  uint64_t block_address)
{
    assert(state != NULL && state->active != NULL);
    cache_write_buffer_entry* entry = calloc(1, sizeof(*entry));
    if (entry == NULL)
        return NULL;
    entry->block_address = block_address;
    entry->request = state->active;
    state->active = NULL;
    /* Take ownership of the dispatch-completion tracker too, freeing
     * state->completion right away: refCache detaches a buffer-eligible
     * write into its own background slot and immediately lets the
     * foreground pipeline start the next queued request in that same tick
     * (confirmed via disassembly of its tick()/coherCallback write-buffer
     * path), rather than waiting for this store's later notification. */
    entry->completion = state->completion;
    state->completion = NULL;

    if (state->write_buffer == NULL)
    {
        state->write_buffer = calloc(1, sizeof(*state->write_buffer));
        if (state->write_buffer == NULL)
        {
            free(entry->request);
            free(entry->completion);
            free(entry);
            return NULL;
        }
    }
    if (state->write_buffer->tail != NULL)
        state->write_buffer->tail->next = entry;
    else
        state->write_buffer->head = entry;
    state->write_buffer->tail = entry;
    ++state->write_buffer->count;
    return entry;
}

cache_write_buffer_entry* cache_write_buffer_pop_head(cache_state* state)
{
    assert(state != NULL && state->write_buffer != NULL
           && state->write_buffer->head != NULL);
    cache_write_buffer_entry* head = state->write_buffer->head;
    state->write_buffer->head = head->next;
    --state->write_buffer->count;
    free(head->request);
    free(head->completion); /* Normally already NULL by the time this retires. */
    free(head);

    if (state->write_buffer->head == NULL)
    {
        free(state->write_buffer);
        state->write_buffer = NULL;
        return NULL;
    }
    return state->write_buffer->head;
}

void cache_write_buffer_destroy(cache_state* state)
{
    if (state == NULL || state->write_buffer == NULL)
        return;
    cache_write_buffer_entry* entry = state->write_buffer->head;
    while (entry != NULL)
    {
        cache_write_buffer_entry* next = entry->next;
        free(entry->request);
        free(entry->completion);
        free(entry);
        entry = next;
    }
    free(state->write_buffer);
    state->write_buffer = NULL;
}
