#include "access.h"
#include "lookup.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static void fail(const char* message)
{
    fprintf(stderr, "teamCache: %s\n", message);
    exit(EXIT_FAILURE);
}

static uint64_t block_address(const cache_state* state, uint64_t address)
{
    return address & ~((uint64_t)state->B - 1);
}

static void touch(cache_state* state, cache_line* line, enum op_type op)
{
    if (state->access_sequence == UINT64_MAX)
        fail("access sequence exhausted");
    line->last_access = ++state->access_sequence;
    if (op == MEM_STORE)
        line->dirty = true;
}

void cache_access_request(cache_state* state, coher* coherence,
                          const trace_op* op, int processor, int64_t tag,
                          void (*callback)(int, int64_t))
{
    if (op == NULL || callback == NULL || processor < 0
        || (op->op != MEM_LOAD && op->op != MEM_STORE) || op->size <= 0)
        fail("invalid memory request");
    if (state->active != NULL)
        fail("overlapping requests require Phase 07 queueing");
    if (state->policy != CACHE_POLICY_LRU || state->write_buffer_mode != 0)
        fail("RRIP and write-buffer accesses belong to later phases");
    uint64_t offset = op->memAddress & ((uint64_t)state->B - 1);
    if ((uint64_t)op->size > (uint64_t)state->B - offset)
        fail("split-line requests require Phase 05");

    cache_request* request = calloc(1, sizeof(*request));
    if (request == NULL)
        fail("request allocation failed");
    *request = (cache_request){.op = *op, .processor = processor,
        .request_tag = tag, .callback = callback,
        .status = REQUEST_WAITING_DATA};
    state->active = request;

    cache_line* line = cache_lookup(state, op->memAddress);
    if (CADSS_VERBOSE)
        printf("[%d] Address: 0x%" PRIx64 " is a %s\n", processor,
               block_address(state, op->memAddress), line != NULL ? "Hit" : "Miss");
    if (line != NULL)
    {
        touch(state, line, request->op.op);
        request->status = REQUEST_READY;
        return;
    }

    if (coherence == NULL || coherence->permReq == NULL)
        fail("missing coherence request interface");
    if (coherence->permReq(false, block_address(state, request->op.memAddress),
                           request->processor))
        fail("lookup miss but coherence already grants permission; check state consistency");
}

void cache_access_event(cache_state* state, int type, int processor,
                        uint64_t address)
{
    if (type == NO_ACTION)
        return;
    if (type != DATA_RECV)
        fail("unsupported coherence event in Phase 03");
    cache_request* request = state->active;
    if (request == NULL || request->status != REQUEST_WAITING_DATA
        || request->processor != processor
        || block_address(state, request->op.memAddress) != address)
        fail("data event does not match the waiting request");

    uint64_t block_number = address >> state->b;
    size_t set_index = (size_t)(block_number & ((uint64_t)state->S - 1));
    for (size_t way = 0; way < state->E; ++way)
    {
        cache_line* line = state->sets[set_index][way];
        if (!line->valid)
        {
            line->tag = block_number >> state->s;
            line->dirty = false;
            touch(state, line, request->op.op);
            line->valid = true;
            request->status = REQUEST_READY;
            return;
        }
    }
    fail("full-set miss requires Phase 04 eviction");
}

void cache_access_tick(cache_state* state, coher* coherence)
{
    cache_request* request = state->active;
    if (request != NULL && request->status == REQUEST_READY)
    {
        /* Detach before calling external code; this completion occurs once. */
        state->active = NULL;
        request->callback(request->processor, request->request_tag);
        free(request);
    }
    /* A callback nested here can mark READY, but cannot complete this tick. */
    coherence->si.tick();
}

void cache_access_destroy(cache_state* state)
{
    free(state->active);
    state->active = NULL;
}
