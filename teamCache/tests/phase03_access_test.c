/* Checks the student's next-tick, dirty, ordering, and ownership contracts.
 * The mock delivers DATA_RECV inside a lower tick, like the real hierarchy.
 */
#include "access.h"
#include "lifecycle.h"
#include "lookup.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

cache* init(cache_sim_args* args);

static cache_state state;
static coher lower;
static void (*event_callback)(int, int, int64_t);
static unsigned int lower_ticks, requests, completions, delivery_tick;
static unsigned int completion_tick;
static uint64_t requested_address;
static int requested_processor, completed_processor;
static int64_t completed_tag;

static void register_callback(void (*callback)(int, int, int64_t))
{
    event_callback = callback;
}

static void direct_event(int type, int processor, int64_t address)
{
    cache_access_event(&state, &lower, type, processor, (uint64_t)address);
}

static uint8_t permission(uint8_t is_read, uint64_t address, int processor)
{
    assert(is_read == 0);
    ++requests;
    requested_address = address;
    requested_processor = processor;
    delivery_tick = lower_ticks + 3;
    return 0;
}

static int lower_tick(void)
{
    ++lower_ticks;
    if (delivery_tick != 0 && lower_ticks == delivery_tick)
    {
        delivery_tick = 0;
        event_callback(DATA_RECV, requested_processor, (int64_t)requested_address);
    }
    return 1;
}

static void complete(int processor, int64_t tag)
{
    ++completions;
    completed_processor = processor;
    completed_tag = tag;
    completion_tick = lower_ticks;
}

static coher lower = {
    .si = {.tick = lower_tick},
    .registerCacheInterface = register_callback,
    .permReq = permission
};

static void issue(enum op_type kind, uint64_t address, int64_t tag)
{
    trace_op* op = calloc(1, sizeof(*op));
    assert(op != NULL);
    op->op = kind;
    op->memAddress = address;
    op->size = 1;
    unsigned int before = completions;
    cache_access_request(&state, &lower, op, 7, tag, complete);
    /* Caller-owned storage disappears immediately, as in processor.c. */
    free(op);
    assert(completions == before);
    assert(state.active == NULL && state.request_queue_head != NULL);
    cache_access_tick(&state, &lower); /* Start on the next cache tick. */
    assert(completions == before && state.active != NULL);
}

static void finish_miss(int64_t tag)
{
    unsigned int before = completions;
    unsigned int sent = requests;
    uint64_t sequence = state.access_sequence;
    cache_access_tick(&state, &lower);
    assert(state.active->status == REQUEST_WAITING_DATA);
    assert(state.access_sequence == sequence && completions == before);
    assert(requests == sent);
    cache_access_tick(&state, &lower);
    assert(state.active->status == REQUEST_READY);
    assert(state.access_sequence == sequence + 1 && completions == before);
    unsigned int received = lower_ticks;
    cache_access_tick(&state, &lower);
    assert(completions == before + 1 && state.active == NULL);
    assert(completion_tick == received); /* Notification precedes lower tick. */
    assert(completed_processor == 7 && completed_tag == tag);
    cache_access_tick(&state, &lower);
    assert(completions == before + 1 && requests == sent);
}

static void finish_hit(int64_t tag)
{
    unsigned int before = completions;
    unsigned int sent = requests;
    assert(state.active->status == REQUEST_READY);
    cache_access_tick(&state, &lower);
    assert(completions == before + 1 && state.active == NULL);
    assert(completed_processor == 7 && completed_tag == tag);
    cache_access_tick(&state, &lower);
    assert(completions == before + 1 && requests == sent);
}

int main(void)
{
    char* argv[] = {"cache", "-s", "0", "-E", "4", "-b", "4"};
    cache_sim_args args = {7, argv, &lower};
    assert(cache_storage_init(&state, &args));
    register_callback(direct_event);
    cache_access_tick(&state, &lower); /* Idle ticks still advance memory. */
    assert(lower_ticks == 1 && completions == 0);

    issue(MEM_LOAD, 0x18, 101);
    assert(requested_address == 0x10 && requests == 1);
    assert(cache_lookup(&state, 0x18) == NULL);
    cache_access_event(&state, &lower, NO_ACTION, 7, 0x10);
    assert(state.active->status == REQUEST_WAITING_DATA);
    finish_miss(101);
    cache_line* a = cache_lookup(&state, 0x10);
    assert(a != NULL && !a->dirty && a->time_stamp == 1);

    issue(MEM_STORE, 0x10, 102);
    assert(a->dirty && a->time_stamp == 2 && requests == 1);
    finish_hit(102);
    issue(MEM_LOAD, 0x1f, 103);
    assert(a->dirty && a->time_stamp == 3 && requests == 1);
    finish_hit(103);

    issue(MEM_STORE, 0x28, 104);
    assert(requested_address == 0x20);
    finish_miss(104);
    cache_line* b = cache_lookup(&state, 0x20);
    assert(b != NULL && b->dirty && b->time_stamp == 4);
    assert(a->time_stamp == 3);
    issue(MEM_LOAD, 0x10, 105);
    assert(a->time_stamp == 5 && b->time_stamp == 4);
    finish_hit(105);

    issue(MEM_LOAD, UINT64_C(0x8000000000000018), INT64_MAX);
    assert(requested_address == UINT64_C(0x8000000000000010));
    finish_miss(INT64_MAX);
    cache_line* high = cache_lookup(&state, UINT64_C(0x8000000000000018));
    assert(high != NULL && high != a && !high->dirty && high->time_stamp == 6);
    cache_access_destroy(&state);
    cache_storage_destroy(&state);

    /* Public destroy owns cleanup of an unfinished request, WAITING or READY. */
    unsigned int before = completions;
    for (int ready = 0; ready <= 1; ++ready)
    {
        cache* interface = init(&args);
        assert(interface != NULL);
        trace_op op = {.op = MEM_LOAD, .memAddress = 0x10, .size = 1};
        interface->memoryRequest(&op, 0, 501, complete);
        if (ready)
            for (int i = 0; i < 3; ++i)
                interface->si.tick();
        assert(completions == before);
        assert(interface->si.destroy() == 0);
        delivery_tick = 0; /* Simulated lower hierarchy also shuts down. */
        assert(destroy() == 0);
    }
    puts("Phase 03 access checks passed (timing, dirty, LRU order, ownership)");
    return 0;
}
