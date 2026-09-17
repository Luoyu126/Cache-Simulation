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

    uint64_t mask = (uint64_t)state->B - 1;
    uint64_t first = op->memAddress & ~mask;
    uint64_t last = (op->memAddress + (remaining - 1)) & ~mask;
    if (last > first && last - first > state->B)
        last = first + state->B;

    state->completion = calloc(1, sizeof(*state->completion));
    if (state->completion == NULL)
        return "request count allocation failed";
    *state->completion = (cache_completion){
        .op = *op,
        .processor = processor,
        .request_tag = tag,
        .callback = callback,
        .first_block = first,
        .last_block = last,
        .total = last == first ? 1 : 2
    };
    return NULL;
}

cache_request* cache_split_take(cache_state* state)
{
    cache_completion* count = state->completion;
    assert(state->active == NULL && count != NULL);
    assert(count->completed < count->total);
    cache_request* request = calloc(1, sizeof(*request));
    if (request == NULL)
        return NULL;

    uint64_t block = count->completed == 0 ? count->first_block
                                           : count->last_block;
    uint64_t start = block;
    if (count->completed == 0)
        start = count->op.memAddress;
    uint64_t end = count->op.memAddress + (uint64_t)count->op.size - 1;
    uint64_t block_end = block + (uint64_t)state->B - 1;
    if (end > block_end)
        end = block_end;

    *request = (cache_request){
        .op = count->op,
        .processor = count->processor,
        .request_tag = count->request_tag,
        .callback = count->callback,
        .status = REQUEST_QUEUED,
        .block_index = count->completed
    };
    request->op.memAddress = start;
    request->op.size = (int)(end - start + 1);
    return request;
}

static bool complete_one(cache_state* state, const cache_request* request)
{
    cache_completion* count = state->completion;
    assert(count != NULL);
    assert(count->processor == request->processor
           && count->request_tag == request->request_tag);
    assert(count->completed < count->total
           && request->block_index == count->completed);
    ++count->completed;
    if (count->completed != count->total)
        return false;
    free(count);
    state->completion = NULL;
    return true;
}

bool cache_split_complete(cache_state* state, const cache_request* request)
{
    assert(request->status == REQUEST_READY);
    return complete_one(state, request);
}

void cache_split_complete_buffered(cache_state* state,
                                   const cache_request* request)
{
    assert(state->completion != NULL && state->completion->total == 1);
    assert(complete_one(state, request));
}
