/* Seed metadata directly: fill/replacement behavior is outside Phase 02.
 * Expected addresses and line pointers are fixed, not decoded by test helpers.
 */
#include "lifecycle.h"
#include "lookup.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned int checks;

/* Every query checks both its result and that all state/pointers are unchanged. */
static void check_lookup(cache_state* state, uint64_t addr, cache_line* expected)
{
    assert(state->S <= 4 && state->E <= 2);
    unsigned char state_before[sizeof(*state)];
    cache_line before[4][2];
    cache_line* lines[4][2];
    cache_line** sets[4];
    memcpy(state_before, state, sizeof(*state));
    for (size_t s = 0; s < state->S; ++s)
    {
        sets[s] = state->sets[s];
        for (size_t w = 0; w < state->E; ++w)
        {
            lines[s][w] = state->sets[s][w];
            before[s][w] = *lines[s][w];
        }
    }

    assert(cache_lookup(state, addr) == expected);
    assert(memcmp(state_before, state, sizeof(*state)) == 0);
    for (size_t s = 0; s < state->S; ++s)
    {
        assert(state->sets[s] == sets[s]);
        for (size_t w = 0; w < state->E; ++w)
        {
            cache_line* line = state->sets[s][w];
            assert(line == lines[s][w]);
            assert(line->valid == before[s][w].valid);
            assert(line->tag == before[s][w].tag);
            assert(line->last_access == before[s][w].last_access);
            assert(line->dirty == before[s][w].dirty);
        }
    }
    ++checks;
}

static void check_four_sets(void)
{
    cache_state state = {0};
    char* argv[] = {"cache", "-s", "2", "-E", "2", "-b", "4", NULL};
    cache_sim_args args = {7, argv, NULL};
    assert(cache_storage_init(&state, &args));

    /* Initially invalid tag-zero lines must not count as hits. */
    check_lookup(&state, 0, NULL);
    /* A valid tag in the wrong set must not count either. */
    *state.sets[0][0] = (cache_line){.valid = true, .tag = 0, .last_access = 9};
    check_lookup(&state, 0x10, NULL);

    /* Invalid way 0 must not prevent a hit in way 1. */
    *state.sets[1][1] = (cache_line){
        .valid = true, .tag = 0, .last_access = 37, .dirty = true
    };
    check_lookup(&state, 0x10, state.sets[1][1]);
    check_lookup(&state, 0x18, state.sets[1][1]);
    check_lookup(&state, 0x1f, state.sets[1][1]);
    check_lookup(&state, 0x20, NULL);
    check_lookup(&state, 0x50, NULL); /* Same set, different tag; one empty way. */

    *state.sets[1][0] = (cache_line){.valid = true, .tag = 1, .last_access = 42};
    check_lookup(&state, 0x50, state.sets[1][0]);
    check_lookup(&state, 0x18, state.sets[1][1]); /* Hit in a full set. */
    check_lookup(&state, 0x90, NULL); /* Miss in a full set. */

    /* High and low addresses differ only in bits a 32-bit decode would lose. */
    *state.sets[3][0] = (cache_line){.valid = true, .tag = UINT64_C(0x3ffffff)};
    *state.sets[3][1] = (cache_line){
        .valid = true, .tag = UINT64_C(0x03ffffffffffffff), .dirty = true
    };
    check_lookup(&state, UINT64_C(0xffffffff), state.sets[3][0]);
    check_lookup(&state, UINT64_MAX, state.sets[3][1]);
    check_lookup(&state, UINT64_C(0x800000000000003f), NULL);
    *state.sets[3][1] = (cache_line){
        .valid = true, .tag = UINT64_C(0x0200000000000000)
    };
    check_lookup(&state, UINT64_C(0x800000000000003f), state.sets[3][1]);
    cache_storage_destroy(&state);
}

static void check_single_set(void)
{
    cache_state state = {0};
    char* argv[] = {"cache", "-s", "0", "-E", "1", "-b", "10", NULL};
    cache_sim_args args = {7, argv, NULL};
    assert(cache_storage_init(&state, &args));
    check_lookup(&state, UINT64_MAX, NULL);
    *state.sets[0][0] = (cache_line){.valid = true, .tag = 1, .last_access = 99};
    check_lookup(&state, 0x400, state.sets[0][0]);
    check_lookup(&state, 0x7ff, state.sets[0][0]);
    check_lookup(&state, 0x800, NULL);
    cache_storage_destroy(&state);
}

int main(void)
{
    check_four_sets();
    check_single_set();
    printf("Phase 02 lookup checks passed: %u (including read-only snapshots)\n", checks);
    return 0;
}
