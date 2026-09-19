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

/* refCache treats an eviction needed by a later split block as an
 * invalidation-only operation: it waits for the victim transaction but does
 * not fetch/install the requested block afterward. Keep this state distinct
 * from an ordinary eviction so its acknowledgement cannot fall through to
 * fetch(). */
static void begin_split_eviction_only(cache_state* state, coher* coherence,
                                      cache_request* request,
                                      size_t set_index)
{
    cache_line* victim = cache_replacement_target(state, set_index);
    if (!victim->valid)
        fail("split eviction-only path requires a full set");

    request->target = victim;
    request->victim_address = ((victim->tag << state->s)
                               | (uint64_t)set_index) << state->b;
    request->status = REQUEST_WAITING_SPLIT_EVICTION;
    trace_access(state, request, "Hit");

    if (coherence == NULL || coherence->invlReq == NULL)
        fail("missing coherence eviction interface");
    if (!coherence->invlReq(request->victim_address, request->processor))
        request->status = REQUEST_READY;
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

    /* Reference-simulator quirk: a later split block facing a full set waits
     * for the selected victim's invalidation, reports Hit, and completes
     * without fetching or installing the requested block. */
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
            begin_split_eviction_only(state, coherence, request, set_index);
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

    /* If that request immediately became a buffered write, cache_write_buffer_
     * push() just freed state->active (and, with its own completion tracker
     * now detached, state->completion too): refCache's tick() detaches a
     * buffer-eligible write into its own background slot and falls through,
     * in that same tick, to pop and start the next queued request (confirmed
     * via disassembly of its "jmp 2e07" write-buffer path) rather than
     * leaving the foreground idle until the next tick. It only ever does
     * this one extra hop per tick, so this is a single retry, not a loop. */
    if (current_request_idle(state) && state->request_queue_head != NULL)
    {
        request = cache_request_take(state);
        error = cache_split_prepare(state, &request->op,
            request->processor, request->request_tag, request->callback);
        free(request);
        if (error != NULL)
            fail(error);
        start_next_block(state, coherence);
    }
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

    /* Find whichever of foreground/background this event is for, by asking
     * what THAT request is currently waiting on rather than trusting the
     * event's type alone: per cadss_public issue #17 ("DATA_RECV is more of
     * an ACK that the permReq / invlReq is complete"), DATA_RECV is not
     * exclusively a fetch acknowledgement - it can also ack a pending
     * eviction's invlReq, in place of a separate FLUSH_COMPLETE/NO_ACTION. */
    cache_request* request = NULL;
    if (foreground != NULL && foreground->processor == processor
        && ((foreground->status == REQUEST_WAITING_EVICTION
             || foreground->status == REQUEST_WAITING_SPLIT_EVICTION)
            ? foreground->victim_address == address
            : foreground->status == REQUEST_WAITING_DATA
              && block_address(state, foreground->op.memAddress) == address))
        request = foreground;
    if (background != NULL && background->processor == processor
        && ((background->status == REQUEST_WAITING_EVICTION
             || background->status == REQUEST_WAITING_SPLIT_EVICTION)
            ? background->victim_address == address
            : background->status == REQUEST_WAITING_DATA
              && block_address(state, background->op.memAddress) == address))
    {
        if (request != NULL)
            fail("coherence event matches foreground and write buffer");
        request = background;
    }

    if (type == NO_ACTION && request == NULL)
        return; /* An eviction completed with nothing else pending on it. */
    if (request == NULL)
        fail("coherence event does not match any waiting request");

    if (request->status == REQUEST_WAITING_EVICTION
        || request->status == REQUEST_WAITING_SPLIT_EVICTION)
    {
        if (type != FLUSH_COMPLETE && type != NO_ACTION && type != DATA_RECV)
            fail("unsupported coherence event for a pending eviction");
        if (request->status == REQUEST_WAITING_SPLIT_EVICTION)
        {
            request->status = REQUEST_READY;
            return;
        }
        cache_eviction_complete(request);
        fetch(state, coherence, request);
        return;
    }

    /* Otherwise request->status == REQUEST_WAITING_DATA. */
    if (type != DATA_RECV)
        fail("unsupported coherence event for a pending fetch");
    complete_fetch(state, coherence, request, address);
}

void cache_access_tick(cache_state* state, coher* coherence)
{
    bool acknowledged_buffer = false;
    /* The processor is notified of a buffered write only once its background
     * fetch actually finishes (data_complete), not merely once it is
     * admitted: refCache's own buffered-write latency (confirmed empirically
     * against the real long.trace: teamCache ran a constant ~97 ticks faster
     * per buffered write before this fix, regardless of how many independent
     * accesses followed it) matches an ordinary miss's full round trip. The
     * write buffer's actual benefit is that OTHER, later requests do not have
     * to wait behind this one (see start_next_request's single retry and the
     * detached per-entry completion above) - not that this store itself
     * skips its own memory latency. Only the head entry is ever "started",
     * so it is the only one that can ever be data_complete. */
    if (state->write_buffer != NULL)
    {
        cache_write_buffer_entry* head = state->write_buffer->head;
        if (head != NULL && !head->processor_notified && head->data_complete)
        {
            cache_split_complete_buffered(&head->completion, head->request);
            head->processor_notified = true;
            head->request->callback(head->request->processor,
                                    head->request->request_tag);
            acknowledged_buffer = true;
            write_buffer_advance(state, coherence);
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
