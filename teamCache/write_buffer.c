#include "write_buffer.h"
#include "split.h"

#include <assert.h>
#include <stdlib.h>

bool cache_write_buffer_eligible(const cache_state* state,
                                 const cache_request* request)
{
    assert(state != NULL && request != NULL);
    assert(state->completion != NULL);
    return state->write_buffer_mode == 1
        && state->write_buffer == NULL
        && request->op.op == MEM_STORE
        && state->completion->total == 1;
}

const char* cache_write_buffer_adopt(cache_state* state)
{
    assert(state != NULL && state->active != NULL);
    assert(state->write_buffer == NULL);
    cache_write_buffer* buffer = calloc(1, sizeof(*buffer));
    if (buffer == NULL)
        return "write buffer allocation failed";

    buffer->request = state->active;
    state->active = NULL;
    state->write_buffer = buffer;
    return NULL;
}

void cache_write_buffer_destroy(cache_state* state)
{
    if (state == NULL || state->write_buffer == NULL)
        return;
    free(state->write_buffer->request);
    free(state->write_buffer);
    state->write_buffer = NULL;
}
