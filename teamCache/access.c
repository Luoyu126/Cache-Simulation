#include "access.h"
#include "lookup.h"
#include "eviction.h"
#include "replacement.h"
#include "request_queue.h"
#include "split.h"

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

static void mark_dirty(cache_line* line, enum op_type op)
{
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
    if (request->block_index != 0)
    {
        /* Reference labels the later half of one split processor access specially. */
        printf("  [%d] Address: 0x%" PRIx64 " is also a Hit\n",
               request->processor, address);
        return;
    }
    /* Match the reference's null-address spelling without pointer casts. */
    if (address == 0)
        printf("[%d] Address: (nil) is a %s\n", request->processor, outcome);
    else
        printf("[%d] Address: 0x%" PRIx64 " is a %s\n",
               request->processor, address, outcome);
}

/* Only the selected head performs lookup, replacement or lower requests. */
static void start_next_block(cache_state* state, coher* coherence)
{
    cache_request* request = cache_split_take(state);
    state->active = request;
    request->status = REQUEST_WAITING_DATA;
    const trace_op* op = &request->op;

    cache_line* line = cache_lookup(state, op->memAddress);
    if (line != NULL)
    {
        trace_access(state, "Hit");
        cache_replacement_hit(state, line);
        mark_dirty(line, request->op.op);
        request->status = REQUEST_READY;
        return;
    }

    bool waiting = cache_eviction_prepare(state, coherence);
    trace_access(state, request->status == REQUEST_WAITING_EVICTION
                 ? (request->target->dirty ? "Dirty Evict" : "Evict") : "Miss");
    if (!waiting)
        fetch(state, coherence);
}

static bool current_request_idle(const cache_state* state)
{
    return state->active == NULL && state->completion == NULL
        && state->queue_head == NULL && state->queue_tail == NULL;
}

static void start_next_request(cache_state* state, coher* coherence)
{
    if (!current_request_idle(state) || state->request_queue_head == NULL)
        return;

    original_request* request = cache_request_take(state);
    const char* error = cache_split_prepare(state, &request->op,
        request->processor, request->request_tag, request->callback);
    free(request);
    if (error != NULL)
        fail(error);
    start_next_block(state, coherence);
}

void cache_access_request(cache_state* state, coher* coherence,
                          const trace_op* op, int processor, int64_t tag,
                          void (*callback)(int, int64_t))
{
    (void)coherence; /* Processing begins from a later cache tick. */
    if (op == NULL || callback == NULL || processor < 0
        || (op->op != MEM_LOAD && op->op != MEM_STORE) || op->size <= 0)
        fail("invalid memory request");
    if (state->write_buffer_mode != 0)
        fail("write-buffer accesses belong to Phase 08");
    const char* error = cache_request_enqueue(state, op, processor, tag, callback);
    if (error != NULL)
        fail(error);
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
    cache_replacement_fill(state, line);
    mark_dirty(line, request->op.op);
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
        if (cache_split_complete(state, request))
            request->callback(request->processor, request->request_tag);
        else
            start_next_block(state, coherence);
        /* A new active hit is not retired again in this tick. */
        free(request);
    }
    else
        start_next_request(state, coherence);
    /* A callback can enqueue another request, which remains for a later tick. */
    coherence->si.tick();
}

void cache_access_destroy(cache_state* state)
{
    free(state->active);
    state->active = NULL;
    cache_split_destroy(state);
    cache_request_queue_destroy(state);
}
