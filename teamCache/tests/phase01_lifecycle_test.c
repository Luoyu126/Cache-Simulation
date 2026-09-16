/* Phase 01 only: test the recorded lifecycle guarantees, not cache accesses.
 * Link with --wrap=calloc and --wrap=free for deterministic failure injection.
 */
#include "lifecycle.h"

#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

cache* init(cache_sim_args* args);

void* __real_calloc(size_t count, size_t size);
void __real_free(void* pointer);

static void* allocations[128];
static size_t live_allocations;
static size_t allocation_calls;
static size_t fail_at = SIZE_MAX;

void* __wrap_calloc(size_t count, size_t size)
{
    if (allocation_calls++ == fail_at)
        return NULL;
    void* pointer = __real_calloc(count, size);
    assert(pointer != NULL); /* These tests only request small allocations. */
    assert(live_allocations < sizeof(allocations) / sizeof(allocations[0]));
    allocations[live_allocations++] = pointer;
    return pointer;
}

void __wrap_free(void* pointer)
{
    if (pointer == NULL)
        return;
    size_t i = 0;
    while (i < live_allocations && allocations[i] != pointer)
        ++i;
    /* Also detects double-free and attempts to free borrowed objects. */
    assert(i < live_allocations);
    allocations[i] = allocations[--live_allocations];
    allocations[live_allocations] = NULL;
    __real_free(pointer);
}

static unsigned int registrations;
static unsigned int coherence_ticks;

static void register_callback(void (*callback)(int, int, int64_t))
{
    assert(callback != NULL);
    ++registrations;
}

static int coherence_tick(void)
{
    ++coherence_ticks;
    return 1;
}

static int forbidden_destroy(void)
{
    assert(!"cache must not destroy borrowed coherence");
    return -1;
}

static coher borrowed_coherence = {
    .si = {.tick = coherence_tick, .destroy = forbidden_destroy},
    .registerCacheInterface = register_callback
};

static cache_sim_args arguments(char** argv)
{
    int count = 0;
    while (argv[count] != NULL)
        ++count;
    return (cache_sim_args){count, argv, &borrowed_coherence};
}

static void assert_empty(const cache_state* state)
{
    assert(state->sets == NULL);
    assert(state->s == 0 && state->E == 0 && state->b == 0);
    assert(state->S == 0 && state->B == 0);
    assert(state->policy == CACHE_POLICY_LRU);
    assert(state->rrip_bits == 0 && state->write_buffer_mode == 0);
}

static void check_storage(char** argv, size_t expected_sets, size_t expected_ways,
                          size_t expected_bytes, cache_policy expected_policy,
                          unsigned int expected_rrip, unsigned int expected_wb)
{
    cache_state state = {0};
    cache_sim_args args = arguments(argv);
    assert(cache_storage_init(&state, &args));
    assert(state.S == expected_sets && state.E == expected_ways);
    assert(state.B == expected_bytes);
    assert(((size_t)1 << state.s) == expected_sets);
    assert(((size_t)1 << state.b) == expected_bytes);
    assert(state.policy == expected_policy && state.rrip_bits == expected_rrip);
    assert(state.write_buffer_mode == expected_wb);
    for (size_t s = 0; s < expected_sets; ++s)
    {
        assert(state.sets[s] != NULL);
        for (size_t w = 0; w < expected_ways; ++w)
        {
            cache_line* line = state.sets[s][w];
            assert(line != NULL);
            assert(!line->valid && !line->dirty);
            assert(line->tag == 0 && line->time_stamp == 0);
        }
    }
    /* Mutating one object must not change another line or another set. */
    state.sets[0][0]->tag = UINT64_MAX;
    if (expected_ways > 1)
        assert(state.sets[0][1]->tag == 0);
    if (expected_sets > 1)
        assert(state.sets[1][0]->tag == 0);
    cache_storage_destroy(&state);
    assert_empty(&state);
    cache_storage_destroy(&state);
    assert(live_allocations == 0);
}

static void check_invalid(char** argv)
{
    cache_state state = {0};
    cache_sim_args args = arguments(argv);
    allocation_calls = 0;
    unsigned int before = registrations;
    assert(!cache_storage_init(&state, &args));
    assert_empty(&state);
    assert(init(&args) == NULL);
    assert(registrations == before);
    assert(allocation_calls == 0 && live_allocations == 0);
}

static void check_public_lifecycle(void)
{
    char* argv[] = {"cache", "-s", "1", "-E", "3", "-b", "4", NULL};
    cache_sim_args args = arguments(argv);
    allocation_calls = 0;
    cache* interface = init(&args);
    assert(interface != NULL);
    size_t allocation_count = allocation_calls;
    assert(interface->memoryRequest != NULL);
    assert(interface->si.tick && interface->si.finish && interface->si.destroy);
    assert(interface->dbgEnv.cadssDbgWatchedComp == 0);
    assert(interface->dbgEnv.cadssDbgNotifyState == 0);
    assert(interface->dbgEnv.cadssDbgExternBreak == 0);
    unsigned int before = coherence_ticks;
    assert(interface->si.tick() == 1);
    assert(coherence_ticks == before + 1);
    assert(interface->si.finish(1) == 0);
    unsigned int registered = registrations;
    assert(init(&args) == NULL); /* A second init must not discard live state. */
    assert(registrations == registered && allocation_calls == allocation_count);
    assert(interface->si.destroy() == 0);
    assert(live_allocations == 0);
    assert(destroy() == 0);

    /* Fail each allocation, including set arrays, lines, and public interface. */
    for (size_t failure = 0; failure < allocation_count; ++failure)
    {
        allocation_calls = 0;
        fail_at = failure;
        registered = registrations;
        assert(init(&args) == NULL);
        assert(registrations == registered && live_allocations == 0);
        assert(destroy() == 0);
        fail_at = SIZE_MAX;
        interface = init(&args);
        assert(interface != NULL && registrations == registered + 1);
        assert(interface->si.destroy() == 0 && live_allocations == 0);
    }
    printf("Allocation-failure checkpoints passed: %zu\n", allocation_count);

    char* other[] = {"cache", "-s", "0", "-E", "1", "-b", "10", NULL};
    args = arguments(other);
    interface = init(&args);
    assert(interface != NULL);
    assert(interface->si.tick() == 1);
    assert(interface->si.destroy() == 0 && live_allocations == 0);
    registered = registrations;
    assert(init(NULL) == NULL);
    args.coherComp = NULL;
    assert(init(&args) == NULL && registrations == registered);
}

int main(void)
{
    char* minimum[] = {"cache", "-s", "0", "-E", "1", "-b", "4", NULL};
    char* larger[] = {"cache", "-s", "2", "-E", "3", "-b", "10", NULL};
    char* options[] = {"cache", "-s", "1", "-E", "2", "-b", "5",
                      "-R", "3", "-w", "1", "-i", "4", "-u", "2", NULL};
    /* Exercise repeated getopt use, including different configurations. */
    optind = 7;
    opterr = 1;
    check_storage(minimum, 1, 1, 16, CACHE_POLICY_LRU, 0, 0);
    assert(optind == 7 && opterr == 1);
    check_storage(larger, 4, 3, 1024, CACHE_POLICY_LRU, 0, 0);
    check_storage(options, 2, 2, 32, CACHE_POLICY_RRIP, 3, 1);
    check_storage(minimum, 1, 1, 16, CACHE_POLICY_LRU, 0, 0);

    char* invalid[][8] = {
        {"cache", "-s", "64", "-E", "1", "-b", "4", NULL},
        {"cache", "-s", "61", "-E", "1", "-b", "4", NULL},
        {"cache", "-s", "60", "-E", "16", "-b", "4", NULL},
        {"cache", "-s", "0", "-E", "18446744073709551615", "-b", "4", NULL},
        {"cache", "-s", "0", "-E", "0", "-b", "4", NULL},
        {"cache", "-s", "-1", "-E", "1", "-b", "4", NULL},
        {"cache", "-s", "0x1", "-E", "1", "-b", "4", NULL},
        {"cache", "-s", "0", "-E", "1", "-b", "3", NULL},
        {"cache", "-s", "0", "-E", "1", "-b", "11", NULL},
        {"cache", "-s", "99999999999999999999999999", NULL},
        {"cache", "-E", "1", "-b", "4", NULL},
        {"cache", "-s", "0", "-b", "4", NULL},
        {"cache", "-s", "0", "-E", "1", NULL},
        {"cache", "-s", NULL},
        {"cache", "-q", "1", NULL}
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        check_invalid(invalid[i]);

    check_public_lifecycle();
    assert(live_allocations == 0);
    puts("Phase 01 lifecycle checks passed");
    return 0;
}
