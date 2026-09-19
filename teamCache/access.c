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

/* Defined further down; forward-declared so fetch() can call it when
 * permReq completes synchronously instead of via a later DATA_RECV. */
static void write_buffer_advance(cache_state* state, coher* coherence);

/* Fills request->target with the data that just arrived for address, and
 * lets write-buffer bookkeeping react if this was the buffered background
 * entry. Shared by the immediate-grant path in fetch() and the deferred
 * DATA_RECV path in cache_access_event(), since permReq can resolve either
 * way per the coherence interface contract. */
static void complete_fetch(cache_state* state, coher* coherence,
                           cache_request* request, uint64_t address)
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

    cache_write_buffer_entry* head_entry = state->write_buffer == NULL
        ? NULL : state->write_buffer->head;
    if (head_entry != NULL && head_entry->request == request)
    {
        head_entry->data_complete = true;
        if (head_entry->processor_notified)
            write_buffer_advance(state, coherence);
    }
}

static void fetch(cache_state* state, coher* coherence, cache_request* request)
{
    request->status = REQUEST_WAITING_DATA;
    if (coherence == NULL || coherence->permReq == NULL)
        fail("missing coherence request interface");
    uint64_t address = block_address(state, request->op.memAddress);
    /* Per the coherence interface: true means permission is granted right
     * now (no DATA_RECV will follow) and the cache can proceed immediately;
     * false means the request is queued and a later DATA_RECV completes it. */
    if (coherence->permReq(false, address, request->processor))
        complete_fetch(state, coherence, request, address);
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

/* Runs the eviction/fetch handshake for whichever request currently owns
 * the single outstanding memory-request slot (a plain miss, or a write
 * buffer entry that just became the head of the FIFO). A queued entry was
 * already announced as a Miss when it was admitted, so it must not be
 * announced again now that its eviction outcome is actually known. */
static void begin_fill(cache_state* state, coher* coherence,
                       cache_request* request, bool announce)
{
    bool waiting = cache_eviction_prepare(state, coherence, request);
    if (announce)
        trace_access(state, request, request->status == REQUEST_WAITING_EVICTION
                     ? (request->target->dirty ? "Dirty Evict" : "Evict") : "Miss");
    if (!waiting)
        fetch(state, coherence, request);
}

static void start_entry(cache_state* state, coher* coherence,
                        cache_write_buffer_entry* entry, bool announce)
{
    entry->started = true;
    begin_fill(state, coherence, entry->request, announce);
}

static void start_miss(cache_state* state, coher* coherence,
                       cache_request* request)
{
    uint64_t block = block_address(state, request->op.memAddress);

    /* Modes 2+: an entry already filling this block serves the access
     * directly (read from buffer / write coalescing), no new work needed. */
    cache_write_buffer_entry* match = cache_write_buffer_find(state, block);
    if (match != NULL)
    {
        trace_access(state, request, "Hit");
        request->status = REQUEST_READY;
        return;
    }

    /* Reference-simulator quirk (confirmed empirically against refCache): a
     * later sub-block of a split access that would need to evict a valid
     * line is instead treated as a no-op hit - nothing is fetched or
     * replaced, so a later independent access to the same address misses
     * again. Only the split's first sub-block evicts normally. Checked as a
     * plain scan (not cache_replacement_target) so RRIP's aging fallback
     * never runs as a side effect of merely checking for free space.
     *
     * A closer timing match (paying fetch latency, then discarding the
     * result) was tried and rejected: it leaves the coherence component
     * believing this cache still holds the address, since permReq was
     * granted but never released. The next independent access to that same
     * address then trips the "fetch while already granted" assertion in
     * fetch() - a hard crash, on real traces essentially guaranteed to
     * recur before the run ends. Completing instantly with no coherence
     * traffic at all is the only safe option here, at the cost of slightly
     * undercounting ticks for this rare case. */
    if (request->block_index != 0)
    {
        uint64_t block_number = request->op.memAddress >> state->b;
        size_t set_index = (size_t)(block_number & ((uint64_t)state->S - 1));
        bool has_room = false;
        for (size_t way = 0; way < state->E; ++way)
            if (!state->sets[set_index][way]->valid)
            {
                has_room = true;
                break;
            }
        if (!has_room)
        {
            trace_access(state, request, "Hit");
            request->status = REQUEST_READY;
            return;
        }
    }

    if (cache_write_buffer_eligible(state, request))
    {
        cache_write_buffer_entry* entry = cache_write_buffer_push(state, block);
        if (entry == NULL)
            fail("write buffer allocation failed");
        if (state->write_buffer->head == entry)
            start_entry(state, coherence, entry, true); /* Only entry: starts now. */
        else
        {
            /* Queued behind another entry: still a miss now, but its own
             * eviction is decided later, once it becomes the head - that
             * later start must not announce this same access a second time. */
            trace_access(state, request, "Miss");
            entry->request->status = REQUEST_QUEUED;
        }
        return; /* The processor learns of the buffered write next tick. */
    }

    if (state->write_buffer != NULL)
    {
        /* The buffer's head owns the single outstanding request; anything
         * that can't be absorbed by the buffer must wait for a slot. */
        request->status = REQUEST_WAITING_BUFFER;
        return;
    }

    begin_fill(state, coherence, request, true);
}

/* Checks the tag array before falling back to the miss/buffer path. Used
 * both for a freshly split block and for re-trying a request that was
 * parked on the write buffer: the address it wants may have just been
 * filled by the very entry that unblocked it. */
static void resolve_request(cache_state* state, coher* coherence,
                            cache_request* request)
{
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
    state->active = request;
    request->status = REQUEST_WAITING_DATA;
    resolve_request(state, coherence, request);
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

/* Frees a retired head entry and lets whatever needed its slot proceed:
 * the next queued write (if any) starts its own fetch, and a foreground
 * access that was waiting on the buffer gets another try. */
static void write_buffer_advance(cache_state* state, coher* coherence)
{
    cache_write_buffer_entry* next_entry = cache_write_buffer_pop_head(state);
    if (next_entry != NULL)
        /* Already announced as a Miss when it was admitted; don't repeat. */
        start_entry(state, coherence, next_entry, false);

    cache_request* foreground = state->active;
    if (foreground != NULL && foreground->status == REQUEST_WAITING_BUFFER)
        /* The entry that just freed this slot may have filled the very
         * line this request wants; check for a hit before missing again. */
        resolve_request(state, coherence, foreground);
    else if (foreground == NULL)
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
    /* Only the head entry is ever "started", so only it can be mid-flight. */
    cache_write_buffer_entry* head_entry = state->write_buffer == NULL
        ? NULL : state->write_buffer->head;
    cache_request* background = head_entry == NULL ? NULL : head_entry->request;
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

    complete_fetch(state, coherence, request, address);
}

void cache_access_tick(cache_state* state, coher* coherence)
{
    bool acknowledged_buffer = false;
    /* At most one entry is ever admitted-but-unacknowledged at a time: the
     * next admission can't happen until this one's completion count (which
     * shares state->completion with the foreground pipeline) is freed here. */
    if (state->write_buffer != NULL)
    {
        for (cache_write_buffer_entry* entry = state->write_buffer->head;
             entry != NULL; entry = entry->next)
        {
            if (entry->processor_notified)
                continue;
            cache_split_complete_buffered(state, entry->request);
            entry->processor_notified = true;
            entry->request->callback(entry->request->processor,
                                     entry->request->request_tag);
            acknowledged_buffer = true;
            /* The fill can race ahead of this notify tick; finish retiring. */
            if (entry->data_complete && entry == state->write_buffer->head)
                write_buffer_advance(state, coherence);
            break;
        }
    }

    cache_request* request = state->active;
    /* Independent hits (and buffer reads/coalesces, which also become
     * REQUEST_READY) retire regardless of any buffered write in flight. */
    if (!acknowledged_buffer && request != NULL
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
