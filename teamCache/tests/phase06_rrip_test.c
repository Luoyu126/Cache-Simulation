#include "access.h"
#include "lifecycle.h"
#include "lookup.h"
#include "replacement.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static cache_state state;
static coher lower;
static uint64_t requested_address;
static unsigned int fetches, evictions, completions;

static uint8_t permission(uint8_t is_read, uint64_t address, int processor)
{
    assert(is_read == 0 && processor == 3);
    requested_address = address;
    ++fetches;
    return 0;
}

static uint8_t invalidate(uint64_t address, int processor)
{
    assert(processor == 3);
    requested_address = address;
    ++evictions;
    return 0;
}

static int lower_tick(void)
{
    return 1;
}

static void complete(int processor, int64_t tag)
{
    assert(processor == 3 && tag == (int64_t)completions + 1);
    ++completions;
}

static coher lower = {
    .si = {.tick = lower_tick},
    .permReq = permission,
    .invlReq = invalidate
};

static void init_state(char* bits, char* ways)
{
    char* argv[] = {
        "cache", "-s", "0", "-E", ways, "-b", "4", "-R", bits
    };
    cache_sim_args args = {9, argv, &lower};
    assert(cache_storage_init(&state, &args));
    assert(state.policy == CACHE_POLICY_RRIP);
}

static void issue(enum op_type kind, uint64_t address)
{
    trace_op op = {.op = kind, .memAddress = address, .size = 1};
    cache_access_request(&state, &lower, &op, 3, (int64_t)completions + 1,
                         complete);
}

static void receive_and_complete(uint64_t address)
{
    cache_access_event(&state, &lower, DATA_RECV, 3, address);
    assert(state.active != NULL && state.active->status == REQUEST_READY);
    cache_access_tick(&state, &lower);
    assert(state.active == NULL);
}

static void complete_hit(void)
{
    assert(state.active != NULL && state.active->status == REQUEST_READY);
    cache_access_tick(&state, &lower);
    assert(state.active == NULL);
}

static void test_rrip_access_and_victim_flow(void)
{
    init_state("2", "2");

    issue(MEM_LOAD, 0x00);
    assert(fetches == 1 && requested_address == 0x00);
    receive_and_complete(0x00);
    cache_line* first = cache_lookup(&state, 0x00);
    assert(first != NULL && first->time_stamp == 2);

    issue(MEM_LOAD, 0x00);
    assert(first->time_stamp == 0 && fetches == 1);
    complete_hit();

    issue(MEM_STORE, 0x10);
    receive_and_complete(0x10);
    cache_line* second = cache_lookup(&state, 0x10);
    assert(second != NULL && second != first);
    assert(second->time_stamp == 2 && second->dirty);

    /*
     * [0,2] contains no maximum for k=2. Aging produces [1,3], so the
     * second way is selected even though the first way appears first.
     */
    issue(MEM_LOAD, 0x20);
    assert(evictions == 1 && requested_address == 0x20);
    assert(state.active->target == second);
    assert(first->time_stamp == 1 && second->time_stamp == 3);
    receive_and_complete(0x20);
    assert(cache_lookup(&state, 0x00) == first);
    assert(cache_lookup(&state, 0x20) == second);
    assert(second->time_stamp == 2 && !second->dirty);

    cache_access_destroy(&state);
    cache_storage_destroy(&state);
}

static void test_first_invalid_and_first_maximum(void)
{
    init_state("3", "3");
    cache_line* first = state.sets[0][0];
    cache_line* second = state.sets[0][1];
    cache_line* third = state.sets[0][2];

    first->valid = true;
    first->time_stamp = 7;
    second->valid = false;
    third->valid = false;
    assert(cache_replacement_target(&state, 0) == second);

    second->valid = true;
    third->valid = true;
    second->time_stamp = 7;
    third->time_stamp = 6;
    assert(cache_replacement_target(&state, 0) == first);

    cache_storage_destroy(&state);
}

static void test_k_boundaries(void)
{
    init_state("64", "2");
    cache_line* first = state.sets[0][0];
    cache_line* second = state.sets[0][1];
    first->valid = second->valid = true;

    cache_replacement_fill(&state, first);
    cache_replacement_fill(&state, second);
    assert(first->time_stamp == UINT64_MAX - 1);
    assert(second->time_stamp == UINT64_MAX - 1);
    assert(cache_replacement_target(&state, 0) == first);
    assert(first->time_stamp == UINT64_MAX);
    assert(second->time_stamp == UINT64_MAX);
    cache_replacement_hit(&state, second);
    assert(second->time_stamp == 0);
    cache_storage_destroy(&state);

    init_state("1", "1");
    first = state.sets[0][0];
    first->valid = true;
    cache_replacement_fill(&state, first);
    assert(first->time_stamp == 0);
    assert(cache_replacement_target(&state, 0) == first);
    assert(first->time_stamp == 1);
    cache_storage_destroy(&state);
}

static void test_lru_regression(void)
{
    char* argv[] = {"cache", "-s", "0", "-E", "3", "-b", "4"};
    cache_sim_args args = {7, argv, &lower};
    assert(cache_storage_init(&state, &args));
    assert(state.policy == CACHE_POLICY_LRU);

    for (size_t way = 0; way < state.E; ++way)
        state.sets[0][way]->valid = true;
    state.sets[0][0]->time_stamp = 5;
    state.sets[0][1]->time_stamp = 2;
    state.sets[0][2]->time_stamp = 2;
    assert(cache_replacement_target(&state, 0) == state.sets[0][1]);

    cache_replacement_hit(&state, state.sets[0][0]);
    assert(state.sets[0][0]->time_stamp == 1);
    cache_replacement_fill(&state, state.sets[0][2]);
    assert(state.sets[0][2]->time_stamp == 2);
    cache_storage_destroy(&state);
}

int main(void)
{
    test_rrip_access_and_victim_flow();
    test_first_invalid_and_first_maximum();
    test_k_boundaries();
    test_lru_regression();
    puts("Phase 06 RRIP checks passed (updates, aging, victims, boundaries, LRU)");
    return 0;
}
