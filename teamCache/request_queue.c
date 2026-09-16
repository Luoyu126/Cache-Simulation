#include "request_queue.h"

#include <assert.h>
#include <stdlib.h>

const char* cache_request_enqueue(cache_state* state, const trace_op* op,
                                  int processor, int64_t tag,
                                  void (*callback)(int, int64_t))
{
    original_request* request = calloc(1, sizeof(*request));
    if (request == NULL)
        return "processor request queue allocation failed";

    *request = (original_request){
        .op = *op,
        .processor = processor,
        .request_tag = tag,
        .callback = callback
    };
    if (state->request_queue_tail != NULL)
        state->request_queue_tail->next = request;
    else
        state->request_queue_head = request;
    state->request_queue_tail = request;
    return NULL;
}

original_request* cache_request_take(cache_state* state)
{
    assert(state != NULL && state->request_queue_head != NULL);
    original_request* request = state->request_queue_head;
    state->request_queue_head = request->next;
    if (state->request_queue_head == NULL)
        state->request_queue_tail = NULL;
    request->next = NULL;
    return request;
}

void cache_request_queue_destroy(cache_state* state)
{
    while (state->request_queue_head != NULL)
    {
        original_request* request = state->request_queue_head;
        state->request_queue_head = request->next;
        free(request);
    }
    state->request_queue_tail = NULL;
}
