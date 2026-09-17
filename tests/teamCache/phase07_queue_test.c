#include "access.h"
#include "lifecycle.h"
#include "request_queue.h"
#include "split.h"

#include <assert.h>
#include <stdio.h>

static cache_state state;
static coher lower;
static void (*event_callback)(int, int, int64_t);
static unsigned int lower_ticks, deadline, fetches, completions;
static uint64_t deadline_address, fetch_address[4];
static int deadline_processor;
static int completed_processor[4];
static int64_t completed_tag[4];
static unsigned int completed_tick[4];

static void register_callback(void (*callback)(int, int, int64_t))
{
    event_callback = callback;
}

static uint8_t permission(uint8_t is_read, uint64_t address, int processor)
{
    assert(is_read == 0 && deadline == 0 && fetches < 4);
    fetch_address[fetches++] = address;
    deadline = lower_ticks + 2;
    deadline_address = address;
    deadline_processor = processor;
    return 0;
}

static int lower_tick(void)
{
    ++lower_ticks;
    if (deadline != 0 && lower_ticks == deadline)
    {
        deadline = 0;
        event_callback(DATA_RECV, deadline_processor, (int64_t)deadline_address);
    }
    return 1;
}

static void complete(int processor, int64_t tag)
{
    assert(completions < 4);
    completed_processor[completions] = processor;
    completed_tag[completions] = tag;
    completed_tick[completions] = lower_ticks;
    ++completions;
}

static void direct_event(int type, int processor, int64_t address)
{
    cache_access_event(&state, &lower, type, processor, (uint64_t)address);
}

static coher lower = {
    .si = {.tick = lower_tick},
    .registerCacheInterface = register_callback,
    .permReq = permission
};

static void setup(void)
{
    char* argv[] = {"cache", "-s", "0", "-E", "4", "-b", "4"};
    cache_sim_args args = {7, argv, &lower};
    assert(cache_storage_init(&state, &args));
    register_callback(direct_event);
    lower_ticks = deadline = fetches = completions = 0;
}

static void issue(enum op_type kind, uint64_t address, int size,
                  int processor, int64_t tag)
{
    trace_op op = {.op = kind, .memAddress = address, .size = size};
    cache_access_request(&state, &lower, &op, processor, tag, complete);
    /* Prove queued requests do not retain the caller-owned trace_op address. */
    op.memAddress = UINT64_MAX;
    op.size = 1;
}

static void test_fifo_and_split_transition(void)
{
    setup();

    issue(MEM_LOAD, 0x10, 1, 1, 11);
    issue(MEM_LOAD, 0x10, 1, 2, 22);
    issue(MEM_LOAD, 0x1f, 2, 3, 33);

    assert(fetches == 0 && state.active == NULL);
    assert(state.request_queue_head != NULL);
    assert(state.request_queue_head->processor == 1);
    assert(state.request_queue_head->op.memAddress == 0x10);
    assert(state.request_queue_head->next->processor == 2);
    assert(state.request_queue_head->next->op.memAddress == 0x10);
    assert(state.request_queue_tail != NULL);
    assert(state.request_queue_tail->processor == 3);
    assert(state.request_queue_tail->op.memAddress == 0x1f);

    cache_access_tick(&state, &lower); /* Start A one tick after arrival. */
    assert(fetches == 1 && fetch_address[0] == 0x10);
    assert(state.active != NULL && state.active->processor == 1);
    cache_access_tick(&state, &lower); /* A receives data and becomes READY. */
    assert(completions == 0 && state.active->processor == 1);

    cache_access_tick(&state, &lower); /* Complete A; leave B queued. */
    assert(completions == 1 && completed_processor[0] == 1);
    assert(completed_tag[0] == 11 && completed_tick[0] == 2);
    assert(state.active == NULL && state.request_queue_head->processor == 2);

    cache_access_tick(&state, &lower); /* Start hit B on the following tick. */
    assert(state.active != NULL && state.active->processor == 2);
    assert(state.active->status == REQUEST_READY);
    assert(completions == 1);

    cache_access_tick(&state, &lower); /* Complete B; leave C queued. */
    assert(completions == 2 && completed_processor[1] == 2);
    assert(completed_tag[1] == 22 && completed_tick[1] == 4);
    assert(state.active == NULL && state.request_queue_head->processor == 3);

    cache_access_tick(&state, &lower); /* Start C's low hit next tick. */
    assert(state.active != NULL && state.active->processor == 3);
    assert(state.active->op.memAddress == 0x1f);
    assert(state.active->status == REQUEST_READY);
    assert(state.queue_head == NULL && state.completion->total == 2);
    assert(state.completion->last_block == 0x20);
    assert(state.request_queue_head == NULL);

    cache_access_tick(&state, &lower); /* Retire low block; start high miss. */
    assert(completions == 2 && fetches == 2 && fetch_address[1] == 0x20);
    assert(state.active->processor == 3);
    cache_access_tick(&state, &lower); /* High block receives data. */
    assert(state.active->status == REQUEST_READY && completions == 2);
    cache_access_tick(&state, &lower); /* Complete C once after both blocks. */
    assert(completions == 3 && completed_processor[2] == 3);
    assert(completed_tag[2] == 33 && completed_tick[2] == 8);
    assert(state.active == NULL && state.completion == NULL);
    assert(state.queue_head == NULL && state.request_queue_head == NULL);

    cache_access_destroy(&state);
    cache_storage_destroy(&state);
}

static void test_destroy_releases_both_queues(void)
{
    setup();
    issue(MEM_LOAD, 0x00, 1, 1, 41);
    issue(MEM_LOAD, 0x1f, 2, 2, 42);
    issue(MEM_STORE, 0x30, 1, 3, 43);
    assert(state.active == NULL && state.request_queue_head != NULL);
    cache_access_tick(&state, &lower);
    assert(state.active != NULL && state.request_queue_head != NULL);

    cache_access_destroy(&state);
    assert(state.active == NULL && state.queue_head == NULL);
    assert(state.completion == NULL && state.request_queue_head == NULL);
    assert(state.request_queue_tail == NULL && completions == 0);
    deadline = 0;
    cache_storage_destroy(&state);
}

int main(void)
{
    test_fifo_and_split_transition();
    test_destroy_releases_both_queues();
    puts("Phase 07 queue checks passed (FIFO, ownership, split order, timing, cleanup)");
    return 0;
}
