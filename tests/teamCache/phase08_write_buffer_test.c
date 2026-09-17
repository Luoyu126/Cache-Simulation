#include "access.h"
#include "lifecycle.h"
#include "lookup.h"
#include "split.h"
#include "write_buffer.h"

#include <assert.h>
#include <stdio.h>

static cache_state state;
static coher lower;
static void (*event_callback)(int, int, int64_t);
static unsigned int lower_ticks, permissions, evictions, completions;
static uint64_t permission_address[8], eviction_address[8];
static int completed_processor[8];
static int64_t completed_tag[8];
static bool delayed_eviction;

static void direct_event(int type, int processor, int64_t address)
{
    cache_access_event(&state, &lower, type, processor, (uint64_t)address);
}

static void register_callback(void (*callback)(int, int, int64_t))
{
    event_callback = callback;
}

static uint8_t permission(uint8_t is_read, uint64_t address, int processor)
{
    assert(is_read == 0 && processor >= 0 && permissions < 8);
    permission_address[permissions++] = address;
    return 0;
}

static uint8_t invalidate(uint64_t address, int processor)
{
    assert(processor >= 0 && evictions < 8);
    eviction_address[evictions++] = address;
    return delayed_eviction ? 1 : 0;
}

static int lower_tick(void)
{
    ++lower_ticks;
    return 1;
}

static void complete(int processor, int64_t tag)
{
    assert(completions < 8);
    completed_processor[completions] = processor;
    completed_tag[completions] = tag;
    ++completions;
}

static coher lower = {
    .si = {.tick = lower_tick},
    .registerCacheInterface = register_callback,
    .permReq = permission,
    .invlReq = invalidate
};

static void setup(char* ways, bool buffered)
{
    char* argv_buffered[] = {
        "cache", "-s", "0", "-E", ways, "-b", "4", "-w", "1"
    };
    char* argv_normal[] = {"cache", "-s", "0", "-E", ways, "-b", "4"};
    cache_sim_args args = buffered
        ? (cache_sim_args){9, argv_buffered, &lower}
        : (cache_sim_args){7, argv_normal, &lower};
    assert(cache_storage_init(&state, &args));
    register_callback(direct_event);
    lower_ticks = permissions = evictions = completions = 0;
    delayed_eviction = false;
}

static void issue(enum op_type kind, uint64_t address, int size,
                  int processor, int64_t tag)
{
    trace_op op = {.op = kind, .memAddress = address, .size = size};
    cache_access_request(&state, &lower, &op, processor, tag, complete);
}

static void finish_foreground_data(int processor, uint64_t address)
{
    event_callback(DATA_RECV, processor, (int64_t)address);
    assert(state.active != NULL && state.active->status == REQUEST_READY);
    cache_access_tick(&state, &lower);
}

static void install_initial_line(void)
{
    issue(MEM_LOAD, 0x00, 4, 0, 1);
    assert(state.active == NULL);
    cache_access_tick(&state, &lower);
    assert(state.active != NULL && state.write_buffer == NULL);
    assert(permissions == 1 && permission_address[0] == 0x00);
    finish_foreground_data(0, 0x00);
    assert(completions == 1 && completed_tag[0] == 1);
}

static void test_early_store_callback_and_queued_hit(void)
{
    setup("4", true);
    install_initial_line();

    issue(MEM_STORE, 0x10, 4, 0, 2);
    cache_access_tick(&state, &lower);
    assert(state.active == NULL && state.write_buffer != NULL);
    assert(state.write_buffer->request->status == REQUEST_WAITING_DATA);
    assert(permissions == 2 && permission_address[1] == 0x10);
    assert(completions == 1 && state.completion != NULL);

    cache_access_tick(&state, &lower);
    assert(completions == 2 && completed_tag[1] == 2);
    assert(state.write_buffer != NULL);
    assert(state.write_buffer->processor_notified);
    assert(state.completion == NULL);

    issue(MEM_LOAD, 0x00, 4, 0, 3);
    cache_access_tick(&state, &lower);
    assert(state.active != NULL && state.active->status == REQUEST_READY);
    assert(state.request_queue_head == NULL);
    cache_access_tick(&state, &lower);
    assert(state.active != NULL && state.active->status == REQUEST_READY);
    assert(completions == 2 && permissions == 2);

    event_callback(DATA_RECV, 0, 0x10);
    assert(state.write_buffer == NULL);
    cache_line* stored = cache_lookup(&state, 0x10);
    assert(stored != NULL && stored->dirty);
    assert(state.active != NULL && state.active->status == REQUEST_READY);
    assert(state.active->op.memAddress == 0x00);
    assert(completions == 2);

    cache_access_tick(&state, &lower);
    assert(completions == 3 && completed_tag[2] == 3);
    assert(state.active == NULL && state.request_queue_head == NULL);
    cache_access_destroy(&state);
    cache_storage_destroy(&state);
}

static void test_delayed_background_eviction(void)
{
    setup("1", true);
    cache_line* victim = state.sets[0][0];
    *victim = (cache_line){
        .valid = true, .tag = 0, .time_stamp = 1, .dirty = true
    };
    delayed_eviction = true;

    issue(MEM_STORE, 0x10, 4, 2, 21);
    cache_access_tick(&state, &lower);
    assert(state.write_buffer != NULL && state.active == NULL);
    assert(state.write_buffer->request->status == REQUEST_WAITING_EVICTION);
    assert(evictions == 1 && eviction_address[0] == 0x00);
    assert(permissions == 0 && victim->valid);

    cache_access_tick(&state, &lower);
    assert(completions == 1 && completed_processor[0] == 2);
    assert(completed_tag[0] == 21 && victim->valid);

    event_callback(FLUSH_COMPLETE, 2, 0x00);
    assert(!victim->valid && permissions == 1);
    assert(permission_address[0] == 0x10);
    assert(state.write_buffer->request->status == REQUEST_WAITING_DATA);

    event_callback(DATA_RECV, 2, 0x10);
    assert(state.write_buffer == NULL);
    assert(victim->valid && victim->tag == 1 && victim->dirty);
    cache_access_destroy(&state);
    cache_storage_destroy(&state);
}

static void test_foreground_miss_waits_for_buffer(void)
{
    setup("4", true);
    install_initial_line();

    issue(MEM_STORE, 0x10, 4, 0, 2);
    cache_access_tick(&state, &lower);
    cache_access_tick(&state, &lower);
    assert(state.write_buffer != NULL && completions == 2);

    issue(MEM_LOAD, 0x20, 4, 0, 3);
    cache_access_tick(&state, &lower);
    assert(state.active != NULL);
    assert(state.active->status == REQUEST_WAITING_BUFFER);
    assert(state.request_queue_head == NULL && permissions == 2);

    event_callback(DATA_RECV, 0, 0x10);
    assert(state.write_buffer == NULL && state.active != NULL);
    assert(state.active->status == REQUEST_WAITING_DATA);
    assert(permissions == 3 && permission_address[2] == 0x20);
    event_callback(DATA_RECV, 0, 0x20);
    cache_access_tick(&state, &lower);
    assert(completions == 3 && completed_tag[2] == 3);

    cache_access_destroy(&state);
    cache_storage_destroy(&state);
}

static void test_contained_split_and_mode_zero_stores(void)
{
    setup("4", true);
    issue(MEM_STORE, 0x01, 4, 0, 31);
    cache_access_tick(&state, &lower);
    assert(state.write_buffer != NULL && state.active == NULL);
    cache_access_tick(&state, &lower);
    assert(completions == 1 && state.write_buffer->processor_notified);
    event_callback(DATA_RECV, 0, 0x00);
    assert(completions == 1 && completed_tag[0] == 31);
    assert(state.write_buffer == NULL);
    cache_access_destroy(&state);
    cache_storage_destroy(&state);

    setup("4", true);
    issue(MEM_STORE, 0x00, 32, 0, 32);
    cache_access_tick(&state, &lower);
    assert(state.write_buffer == NULL && state.active != NULL);
    assert(state.completion->total == 2 && state.queue_head != NULL);
    cache_access_destroy(&state);
    cache_storage_destroy(&state);

    setup("4", false);
    issue(MEM_STORE, 0x00, 4, 0, 33);
    cache_access_tick(&state, &lower);
    assert(state.write_buffer == NULL && state.active != NULL);
    assert(state.active->status == REQUEST_WAITING_DATA);
    cache_access_destroy(&state);
    cache_storage_destroy(&state);
}

static void test_destroy_occupied_buffer(void)
{
    setup("4", true);
    issue(MEM_STORE, 0x00, 4, 0, 41);
    cache_access_tick(&state, &lower);
    assert(state.write_buffer != NULL && state.completion != NULL);
    cache_access_destroy(&state);
    assert(state.write_buffer == NULL && state.completion == NULL);
    assert(state.active == NULL && completions == 0);
    cache_storage_destroy(&state);
}

int main(void)
{
    test_early_store_callback_and_queued_hit();
    test_delayed_background_eviction();
    test_foreground_miss_waits_for_buffer();
    test_contained_split_and_mode_zero_stores();
    test_destroy_occupied_buffer();
    puts("Phase 08 write-buffer checks passed");
    return 0;
}
