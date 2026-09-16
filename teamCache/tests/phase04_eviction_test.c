/* Exercises selected-set LRU and the student's eviction/fill event ordering. */
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
static unsigned int lower_ticks, deadline, fetches, evictions, completions;
static uint64_t event_address, evicted_address, fetched_address;
static int event_type, event_processor, last_processor;
static int64_t last_tag;
static bool immediate_eviction;
static int eviction_event = FLUSH_COMPLETE;

static void direct_event(int type, int processor, int64_t address)
{
    cache_access_event(&state, &lower, type, processor, (uint64_t)address);
}

static void register_callback(void (*callback)(int, int, int64_t))
{
    event_callback = callback;
}

static void schedule(int type, uint64_t address, int processor)
{
    assert(deadline == 0);
    event_type = type;
    event_address = address;
    event_processor = processor;
    deadline = lower_ticks + 2;
}

static uint8_t permission(uint8_t is_read, uint64_t address, int processor)
{
    assert(is_read == 0);
    ++fetches;
    fetched_address = address;
    schedule(DATA_RECV, address, processor);
    return 0;
}

static uint8_t invalidate(uint64_t address, int processor)
{
    ++evictions;
    evicted_address = address;
    if (immediate_eviction)
        return 0;
    schedule(eviction_event, address, processor);
    return 1;
}

static int lower_tick(void)
{
    ++lower_ticks;
    if (deadline != 0 && lower_ticks == deadline)
    {
        int type = event_type, processor = event_processor;
        uint64_t address = event_address;
        deadline = 0;
        event_callback(type, processor, (int64_t)address);
    }
    return 1;
}

static coher lower = {
    .si = {.tick = lower_tick},
    .registerCacheInterface = register_callback,
    .permReq = permission, .invlReq = invalidate
};

static void complete(int processor, int64_t tag)
{
    ++completions;
    last_processor = processor;
    last_tag = tag;
}

static void issue(enum op_type kind, uint64_t address, int64_t tag)
{
    trace_op* op = calloc(1, sizeof(*op));
    assert(op != NULL);
    *op = (trace_op){.op = kind, .memAddress = address, .size = 1};
    unsigned int before = completions;
    cache_access_request(&state, &lower, op, 9, tag, complete);
    free(op);
    assert(completions == before);
    assert(state.active == NULL && state.request_queue_head != NULL);
    cache_access_tick(&state, &lower); /* Start on the next cache tick. */
    assert(completions == before && state.active != NULL);
}

static void drain(int64_t tag)
{
    unsigned int before = completions;
    unsigned int remaining = 10;
    while (state.active != NULL && remaining-- != 0)
        cache_access_tick(&state, &lower);
    assert(state.active == NULL && completions == before + 1);
    assert(last_processor == 9 && last_tag == tag);
    cache_access_tick(&state, &lower);
    assert(completions == before + 1);
}

static void fixture(char* set_bits, char* ways)
{
    char* argv[] = {"cache", "-s", set_bits, "-E", ways, "-b", "4"};
    cache_sim_args args = {7, argv, &lower};
    assert(cache_storage_init(&state, &args));
    register_callback(direct_event);
    lower_ticks = deadline = fetches = evictions = completions = 0;
    immediate_eviction = false;
    eviction_event = FLUSH_COMPLETE;
}

static void cleanup(void)
{
    cache_access_destroy(&state);
    cache_storage_destroy(&state);
    deadline = 0;
}

static void test_selected_set_and_delayed_eviction(void)
{
    fixture("2", "2");
    issue(MEM_LOAD, 0x00, 1); drain(1); /* Globally oldest, but different set. */
    issue(MEM_LOAD, 0x10, 2); drain(2);
    issue(MEM_STORE, 0x50, 3); drain(3);
    issue(MEM_LOAD, 0x10, 4); drain(4); /* Make 0x50 local LRU. */
    assert(evictions == 0 && fetches == 3);
    cache_line* other = cache_lookup(&state, 0x00);
    cache_line* recent = cache_lookup(&state, 0x10);
    cache_line* victim = cache_lookup(&state, 0x50);
    assert(other->time_stamp < victim->time_stamp);
    assert(!state.sets[2][0]->valid); /* Other-set space must not be used. */
    cache_line snapshot = *victim;
    unsigned int before = completions;
    uint64_t sequence = state.access_sequence;

    issue(MEM_LOAD, 0x98, 5);
    assert(state.active->target == victim && evicted_address == 0x50);
    assert(state.active->op.memAddress == 0x98 && state.active->request_tag == 5);
    assert(state.active->status == REQUEST_WAITING_EVICTION);
    assert(evictions == 1 && fetches == 3);
    cache_access_event(&state, &lower, NO_ACTION, 9, 0x00);
    assert(victim->valid == snapshot.valid && victim->tag == snapshot.tag);
    assert(victim->dirty == snapshot.dirty && victim->time_stamp == snapshot.time_stamp);
    assert(fetches == 3 && completions == before && state.access_sequence == sequence);

    cache_access_tick(&state, &lower); /* Eviction completes; fetch starts here. */
    assert(!victim->valid && victim->tag == snapshot.tag);
    assert(state.active->status == REQUEST_WAITING_DATA);
    assert(fetches == 4 && fetched_address == 0x90 && completions == before);
    assert(cache_lookup(&state, 0x50) == NULL && cache_lookup(&state, 0x90) == NULL);
    cache_access_tick(&state, &lower);
    assert(!victim->valid && completions == before);
    cache_access_tick(&state, &lower); /* New data arrives, not completion yet. */
    assert(state.active->status == REQUEST_READY && completions == before);
    assert(cache_lookup(&state, 0x90) == victim && !victim->dirty);
    assert(victim->time_stamp == sequence + 1);
    drain(5);
    assert(cache_lookup(&state, 0x00) == other && cache_lookup(&state, 0x10) == recent);
    assert(!state.sets[2][0]->valid);
    cleanup();
}

static void test_clean_victim_high_address_and_legacy_event(void)
{
    fixture("1", "1");
    issue(MEM_LOAD, UINT64_C(0x8000000000000018), INT64_MAX); drain(INT64_MAX);
    cache_line* victim = cache_lookup(&state, UINT64_C(0x8000000000000010));
    assert(victim != NULL && !victim->dirty);
    eviction_event = NO_ACTION; /* PDF naming, guarded by address and state. */
    issue(MEM_STORE, 0x38, 7);
    assert(evicted_address == UINT64_C(0x8000000000000010));
    assert(evictions == 1 && victim->valid);
    drain(7);
    assert(cache_lookup(&state, 0x30) == victim && victim->dirty);
    cleanup();
}

static void test_immediate_eviction(void)
{
    fixture("0", "1");
    issue(MEM_STORE, 0x00, 1); drain(1);
    cache_line* victim = cache_lookup(&state, 0x00);
    immediate_eviction = true;
    issue(MEM_LOAD, 0x10, 2);
    assert(evictions == 1 && fetches == 2 && !victim->valid);
    assert(state.active->status == REQUEST_WAITING_DATA);
    drain(2);
    assert(cache_lookup(&state, 0x10) == victim && !victim->dirty);
    cleanup();
}

static void test_destroy_during_eviction(void)
{
    char* argv[] = {"cache", "-s", "0", "-E", "1", "-b", "4"};
    cache_sim_args args = {7, argv, &lower};
    immediate_eviction = false;
    eviction_event = FLUSH_COMPLETE;
    cache* interface = init(&args);
    assert(interface != NULL);
    trace_op op = {.op = MEM_LOAD, .memAddress = 0, .size = 1};
    interface->memoryRequest(&op, 0, 0, complete);
    for (int i = 0; i < 3; ++i)
        interface->si.tick();
    op.memAddress = 0x10;
    interface->memoryRequest(&op, 0, 256, complete);
    unsigned int before = completions;
    interface->si.tick(); /* Eviction is still pending. */
    assert(interface->si.destroy() == 0 && completions == before);
    deadline = 0;
    assert(destroy() == 0);
}

int main(void)
{
    test_selected_set_and_delayed_eviction();
    test_clean_victim_high_address_and_legacy_event();
    test_immediate_eviction();
    test_destroy_during_eviction();
    puts("Phase 04 eviction checks passed (set-local LRU, ordering, metadata, cleanup)");
    return 0;
}
