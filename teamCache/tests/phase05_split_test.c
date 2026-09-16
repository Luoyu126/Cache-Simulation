/* Exercises the confirmed serial split-access queue and one-callback contract. */
#include "access.h"
#include "lifecycle.h"
#include "lookup.h"
#include "split.h"

#include <assert.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static cache_state state;
static coher lower;
static void (*event_callback)(int, int, int64_t);
static unsigned int lower_ticks, fetches, completions, deadline;
static uint64_t deadline_address, fetch_address[8];
static int deadline_processor, completed_processor;
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
    assert(is_read == 0 && deadline == 0 && fetches < 8);
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

static coher lower = {
    .si = {.tick = lower_tick},
    .registerCacheInterface = register_callback,
    .permReq = permission
};

static void complete(int processor, int64_t tag)
{
    ++completions;
    completed_processor = processor;
    completed_tag = tag;
}

static void setup(void)
{
    char* argv[] = {"cache", "-s", "0", "-E", "4", "-b", "4"};
    cache_sim_args args = {7, argv, &lower};
    assert(cache_storage_init(&state, &args));
    register_callback(direct_event);
    lower_ticks = fetches = completions = deadline = 0;
}

static void issue(enum op_type op, uint64_t address, int size, int64_t tag)
{
    trace_op request = {.op = op, .memAddress = address, .size = size};
    cache_access_request(&state, &lower, &request, 3, tag, complete);
    assert(state.active == NULL && state.request_queue_head != NULL);
    cache_access_tick(&state, &lower); /* Start on the next cache tick. */
    assert(state.active != NULL);
}

static void drain(int64_t tag)
{
    for (unsigned int i = 0; state.active != NULL && i < 30; ++i)
        cache_access_tick(&state, &lower);
    assert(state.active == NULL && state.queue_head == NULL);
    assert(state.completion == NULL && completions == 1);
    assert(completed_processor == 3 && completed_tag == tag);
}

int main(int argc, char** argv)
{
    setup();

    /* 0x1f..0x20: miss low block, then miss high block in address order. */
    issue(MEM_LOAD, 0x1f, 2, 91);
    assert(fetches == 1 && fetch_address[0] == 0x10);
    assert(state.queue_head != NULL && state.queue_head->op.memAddress == 0x20);
    cache_access_tick(&state, &lower); /* Low data arrives; READY. */
    assert(state.active->status == REQUEST_READY && completions == 0);
    cache_access_tick(&state, &lower); /* Retire low and start high. */
    assert(fetches == 2 && fetch_address[1] == 0x20 && completions == 0);
    cache_access_tick(&state, &lower); /* High data arrives; READY. */
    assert(completions == 0 && state.active->status == REQUEST_READY);
    cache_access_tick(&state, &lower); /* Only now does original request finish. */
    assert(completions == 1 && state.active == NULL && completed_tag == 91);

    /* Low hit starts high miss in the same retirement tick, but cannot retire it. */
    completions = 0;
    issue(MEM_LOAD, 0x1f, 2, 92);
    assert(state.active->status == REQUEST_READY && fetches == 2);
    cache_access_tick(&state, &lower);
    assert(completions == 0 && state.active != NULL
           && state.active->op.memAddress == 0x20
           && state.active->status == REQUEST_READY);
    cache_access_tick(&state, &lower);
    assert(completions == 1 && state.active == NULL && completed_tag == 92);

    /* Three blocks: store dirties every installed block and still calls once. */
    completions = 0;
    issue(MEM_STORE, 0x0f, 18, 93); /* blocks 0x00, 0x10, 0x20 */
    assert(fetches == 3 && fetch_address[2] == 0x00);
    drain(93);
    assert(cache_lookup(&state, 0x00)->dirty);
    assert(cache_lookup(&state, 0x10)->dirty);
    assert(cache_lookup(&state, 0x20)->dirty);

    /* Supported sizes remain one block when their inclusive range fits. */
    const int sizes[] = {1, 2, 4, 8};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        completions = 0;
        unsigned int before_fetches = fetches;
        issue(MEM_LOAD, 0x21, sizes[i], 100 + (int64_t)i);
        assert(state.completion->total == 1 && state.queue_head == NULL);
        assert(state.active->status == REQUEST_READY && fetches == before_fetches);
        cache_access_tick(&state, &lower);
        assert(completions == 1 && state.active == NULL);
    }

    cache_access_destroy(&state);
    assert(state.active == NULL && state.queue_head == NULL
           && state.completion == NULL);
    cache_storage_destroy(&state);

    /* Destroy owns both an active block and its unstarted split successors. */
    setup();
    issue(MEM_LOAD, 0x1f, 2, 94);
    assert(state.active != NULL && state.queue_head != NULL);
    cache_access_destroy(&state);
    assert(state.active == NULL && state.queue_head == NULL
           && state.completion == NULL);
    cache_storage_destroy(&state);

    /* A wrapped inclusive endpoint is rejected before any block work begins. */
    if (argc == 1)
    {
        pid_t child = fork();
        assert(child >= 0);
        if (child == 0)
        {
            setup();
            issue(MEM_LOAD, UINT64_MAX, 2, 95);
            _Exit(EXIT_SUCCESS);
        }
        int status;
        assert(waitpid(child, &status, 0) == child);
        assert(status != 0);
    }

    (void)argv;
    puts("Phase 05 split-access checks passed");
    return 0;
}
