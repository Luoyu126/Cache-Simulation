#include "access.h"
#include "lookup.h"
#include "eviction.h"
#include "replacement.h"
#include "request_queue.h"
#include "split.h"
#include "write_buffer.h"

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

static void complete_fill(cache_state* state, cache_request* request,
                          uint64_t address)
{
    cache_line* line = request->target;
    if (line == NULL || line->valid)
        fail("fill target is not available");
    line->tag = (address >> state->b) >> state->s;
    line->dirty = false;
    cache_replacement_fill(state, line);
    mark_dirty(line, request->op.op);
    line->valid = true;
    request->status = REQUEST_READY;
    if (state->write_buffer != NULL && state->write_buffer->request == request)
        state->write_buffer->data_complete = true;
}

static void fetch(cache_state* state, coher* coherence, cache_request* request)
{
    request->status = REQUEST_WAITING_DATA;
    if (coherence == NULL || coherence->permReq == NULL)
        fail("missing coherence request interface");
    uint64_t address = block_address(state, request->op.memAddress);
    if (coherence->permReq(false, address, request->processor))
        complete_fill(state, request, address);
}

static void trace_access(const cache_state* state, const cache_request* request,
                         const char* outcome)
{
    if (!CADSS_VERBOSE)
        return;
    uint64_t address = block_address(state, request->op.memAddress);
    if (request->block_index != 0)
    {
        /* Reference labels the later half of one split processor access specially. */
        printf("  [%d] Address: 0x%" PRIx64 " is also a %s\n",
               request->processor, address, outcome);
        return;
    }
    /* Match the reference's null-address spelling without pointer casts. */
    if (address == 0)
        printf("[%d] Address: (nil) is a %s\n", request->processor, outcome);
    else
        printf("[%d] Address: 0x%" PRIx64 " is a %s\n",
               request->processor, address, outcome);
}

static void start_miss(cache_state* state, coher* coherence,
                       cache_request* request)
{
    if (state->write_buffer != NULL)
    {
        request->status = REQUEST_WAITING_BUFFER;
        return;
    }

    if (cache_write_buffer_eligible(state, request))
    {
        const char* error = cache_write_buffer_adopt(state);
        if (error != NULL)
            fail(error);
    }

    bool waiting = cache_eviction_prepare(state, coherence, request);
    trace_access(state, request, request->status == REQUEST_WAITING_EVICTION
                 ? (request->target->dirty ? "Dirty Evict" : "Evict") : "Miss");
    if (!waiting)
        fetch(state, coherence, request);
}

/* Lookup the already-selected active block. A waiting buffer request reuses
 * this path after the background fill, so a same-line wait can become a hit.
 */
static void process_active_block(cache_state* state, coher* coherence)
{
    cache_request* request = state->active;
    cache_line* line = cache_lookup(state, request->op.memAddress);
    if (line != NULL)
    {
        trace_access(state, request, "Hit");
        cache_replacement_hit(state, line);
        mark_dirty(line, request->op.op);
        request->status = REQUEST_READY;
        return;
    }

    start_miss(state, coherence, request);
}

/* Only the selected head performs lookup, replacement or lower requests. */
static void start_next_block(cache_state* state, coher* coherence)
{
    cache_request* request = cache_split_take(state);
    if (request == NULL)
        fail("block request allocation failed");
    state->active = request;
    process_active_block(state, coherence);
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

static void resume_after_write_buffer(cache_state* state, coher* coherence)
{
    if (state->active != NULL && state->active->status == REQUEST_WAITING_BUFFER)
        process_active_block(state, coherence);
    else if (state->active == NULL)
        start_next_request(state, coherence);
}

void cache_access_request(cache_state* state, coher* coherence,
                          const trace_op* op, int processor, int64_t tag,
                          void (*callback)(int, int64_t))
{
    (void)coherence; /* Processing begins from a later cache tick. */
    if (op == NULL || callback == NULL || processor < 0
        || (op->op != MEM_LOAD && op->op != MEM_STORE) || op->size <= 0)
        fail("invalid memory request");
    const char* error = cache_request_enqueue(state, op, processor, tag, callback);
    if (error != NULL)
        fail(error);
}

void cache_access_event(cache_state* state, coher* coherence, int type, int processor,
                        uint64_t address)
{
    cache_request* foreground = state->active;
    cache_request* background = state->write_buffer == NULL
        ? NULL : state->write_buffer->request;
    cache_request* request = NULL;
    if (foreground != NULL
        && foreground->status == REQUEST_WAITING_EVICTION
        && foreground->processor == processor
        && foreground->victim_address == address)
        request = foreground;
    if (background != NULL
        && background->status == REQUEST_WAITING_EVICTION
        && background->processor == processor
        && background->victim_address == address)
    {
        if (request != NULL)
            fail("eviction event matches foreground and write buffer");
        request = background;
    }
    bool matching_eviction = request != NULL;
    if (type == FLUSH_COMPLETE || (type == NO_ACTION && matching_eviction))
    {
        if (!matching_eviction)
            fail("eviction event does not match the waiting request");
        cache_eviction_complete(request);
        fetch(state, coherence, request);
        return;
    }
    if (type == NO_ACTION)
        return;
    if (type != DATA_RECV)
        fail("unsupported coherence event");
    request = NULL;
    if (foreground != NULL && foreground->status == REQUEST_WAITING_DATA
        && foreground->processor == processor
        && block_address(state, foreground->op.memAddress) == address)
        request = foreground;
    if (background != NULL && background->status == REQUEST_WAITING_DATA
        && background->processor == processor
        && block_address(state, background->op.memAddress) == address)
    {
        if (request != NULL)
            fail("data event matches foreground and write buffer");
        request = background;
    }
    if (request == NULL)
        fail("data event does not match the waiting request");

    complete_fill(state, request, address);
    if (request == background)
    {
        cache_write_buffer* buffer = state->write_buffer;
        if (buffer->processor_notified)
        {
            state->write_buffer = NULL;
            free(request);
            free(buffer);
            resume_after_write_buffer(state, coherence);
        }
    }
}

void cache_access_tick(cache_state* state, coher* coherence)
{
    bool acknowledged_buffer = false;
    cache_write_buffer* buffer = state->write_buffer;
    if (buffer != NULL && !buffer->processor_notified)
    {
        cache_request* buffered = buffer->request;
        cache_split_complete_buffered(state, buffered);
        buffer->processor_notified = true;
        buffered->callback(buffered->processor, buffered->request_tag);
        acknowledged_buffer = true;
        if (buffer->data_complete)
        {
            state->write_buffer = NULL;
            free(buffered);
            free(buffer);
            resume_after_write_buffer(state, coherence);
        }
    }

    cache_request* request = state->active;
    if (!acknowledged_buffer && state->write_buffer == NULL
        && request != NULL
        && request->status == REQUEST_READY)
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
    else if (!acknowledged_buffer)
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
    cache_write_buffer_destroy(state);
}
