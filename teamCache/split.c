#include "split.h"

#include <assert.h>
#include <stdlib.h>

void cache_split_destroy(cache_state* state)
{
    while (state->queue_head != NULL)
    {
        cache_request* request = state->queue_head;
        state->queue_head = request->next;
        free(request);
    }
    state->queue_tail = NULL;
    free(state->completion);
    state->completion = NULL;
}

const char* cache_split_prepare(cache_state* state, const trace_op* op,
                                int processor, int64_t tag,
                                void (*callback)(int, int64_t))
{
    assert(state->active == NULL && state->queue_head == NULL
           && state->queue_tail == NULL && state->completion == NULL);
    if (op->size <= 0)
        return "invalid memory request size";
    uint64_t remaining = (uint64_t)op->size;
    if (remaining - 1 > UINT64_MAX - op->memAddress)
        return "memory request range overflows 64-bit address space";
    uint64_t end = op->memAddress + (remaining - 1);
    uint64_t total = (end >> state->b) - (op->memAddress >> state->b) + 1;
    state->completion = calloc(1, sizeof(*state->completion));
    if (state->completion == NULL)
        return "request count allocation failed";
    *state->completion = (cache_completion){.processor = processor,
        .request_tag = tag, .total = total};

    uint64_t address = op->memAddress;
    for (uint64_t index = 0; index < total; ++index)
    {
        cache_request* request = calloc(1, sizeof(*request));
        if (request == NULL)
        {
            cache_split_destroy(state);
            return "block request allocation failed";
        }
        uint64_t available = (uint64_t)state->B
            - (address & ((uint64_t)state->B - 1));
        uint64_t bytes = remaining < available ? remaining : available;
        *request = (cache_request){.op = *op, .processor = processor,
            .request_tag = tag, .callback = callback,
            .status = REQUEST_QUEUED, .block_index = index};
        request->op.memAddress = address;
        request->op.size = (int)bytes; /* At most B, validated in [16,1024]. */
        if (state->queue_tail != NULL)
            state->queue_tail->next = request;
        else
            state->queue_head = request;
        state->queue_tail = request;
        remaining -= bytes;
        /* Do not compute an exclusive endpoint past UINT64_MAX. */
        if (remaining != 0)
            address += bytes;
    }
    assert(remaining == 0);
    return NULL;
}

cache_request* cache_split_take(cache_state* state)
{
    assert(state->active == NULL && state->queue_head != NULL);
    cache_request* request = state->queue_head;
    state->queue_head = request->next;
    if (state->queue_head == NULL)
        state->queue_tail = NULL;
    request->next = NULL;
    return request;
}

static bool complete_one(cache_completion** slot, const cache_request* request)
{
    cache_completion* count = *slot;
    assert(count != NULL);
    assert(count->processor == request->processor
           && count->request_tag == request->request_tag);
    assert(count->completed < count->total
           && request->block_index == count->completed);
    ++count->completed;
    if (count->completed != count->total)
        return false;
    free(count);
    *slot = NULL;
    return true;
}

bool cache_split_complete(cache_state* state, const cache_request* request)
{
    assert(request->status == REQUEST_READY);
    bool done = complete_one(&state->completion, request);
    if (done)
        assert(state->queue_head == NULL && state->queue_tail == NULL);
    return done;
}

/* completion is an entry's own tracker (already detached from
 * state->completion at push time - see cache_write_buffer_push), so this
 * can finalize independently of whatever the foreground pipeline is doing. */
void cache_split_complete_buffered(cache_completion** completion,
                                   const cache_request* request)
{
    assert(*completion != NULL && (*completion)->total == 1);
    assert(complete_one(completion, request));
}
