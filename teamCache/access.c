#include "access.h"
#include "lookup.h"
#include "eviction.h"

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

static void fetch(cache_state* state, coher* coherence)
{
    cache_request* request = state->active;
    request->status = REQUEST_WAITING_DATA;
    if (coherence == NULL || coherence->permReq == NULL)
        fail("missing coherence request interface");
    if (coherence->permReq(false, block_address(state, request->op.memAddress),
                           request->processor))
        fail("lookup miss but coherence already grants permission; check state consistency");
}

static void trace_access(const cache_state* state, const char* outcome)
{
    if (!CADSS_VERBOSE)
        return;
    const cache_request* request = state->active;
    uint64_t address = block_address(state, request->op.memAddress);
    /* Match the reference's null-address spelling without pointer casts. */
    if (address == 0)
        printf("[%d] Address: (nil) is a %s\n", request->processor, outcome);
    else
        printf("[%d] Address: 0x%" PRIx64 " is a %s\n",
               request->processor, address, outcome);
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
    if (line != NULL)
    {
        trace_access(state, "Hit");
        touch(state, line, request->op.op);
        request->status = REQUEST_READY;
        return;
    }

    bool waiting = cache_eviction_prepare(state, coherence);
    trace_access(state, request->status == REQUEST_WAITING_EVICTION
                 ? (request->target->dirty ? "Dirty Evict" : "Evict") : "Miss");
    if (!waiting)
        fetch(state, coherence);
}

void cache_access_event(cache_state* state, coher* coherence, int type, int processor,
                        uint64_t address)
{
    cache_request* request = state->active;
    bool matching_eviction = request != NULL
        && request->status == REQUEST_WAITING_EVICTION
        && request->processor == processor && request->victim_address == address;
    if (type == FLUSH_COMPLETE || (type == NO_ACTION && matching_eviction))
    {
        if (!matching_eviction)
            fail("eviction event does not match the waiting request");
        cache_eviction_complete(state);
        fetch(state, coherence);
        return;
    }
    if (type == NO_ACTION)
        return;
    if (type != DATA_RECV)
        fail("unsupported coherence event");
    if (request == NULL || request->status != REQUEST_WAITING_DATA
        || request->processor != processor
        || block_address(state, request->op.memAddress) != address)
        fail("data event does not match the waiting request");

    cache_line* line = request->target;
    if (line == NULL || line->valid)
        fail("fill target is not available");
    line->tag = (address >> state->b) >> state->s;
    line->dirty = false;
    touch(state, line, request->op.op);
    line->valid = true;
    request->status = REQUEST_READY;
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
