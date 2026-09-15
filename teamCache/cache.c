#include <cache.h>
#include <trace.h>

#include "lifecycle.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>

typedef struct _pendingRequest {
    int64_t tag;
    int8_t procNum;
    void (*memCallback)(int, int64_t);
} pendingRequest;

/* The state object lives here; lifecycle.c only borrows its address. */
static cache_state state = {0};
static cache* self = NULL;
static coher* coherComp = NULL;

int processorCount = 1;
int CADSS_VERBOSE = 0;
static pendingRequest pending = {0};
static int countDown = 0;

void memoryRequest(trace_op* op, int processorNum, int64_t tag,
                   void (*callback)(int, int64_t));
void coherCallback(int type, int procNum, int64_t addr);

cache* init(cache_sim_args* csa)
{
    if (self != NULL || csa == NULL || csa->coherComp == NULL
        || csa->coherComp->registerCacheInterface == NULL
        || csa->coherComp->si.tick == NULL)
    {
        fprintf(stderr, "teamCache: invalid coherence interface or already initialized\n");
        return NULL;
    }
    if (!cache_storage_init(&state, csa))
        return NULL;

    self = calloc(1, sizeof(*self));
    if (self == NULL)
    {
        fprintf(stderr, "teamCache: public interface allocation failed\n");
        cache_storage_destroy(&state);
        return NULL;
    }
    self->memoryRequest = memoryRequest;
    self->si.tick = tick;
    self->si.finish = finish;
    self->si.destroy = destroy;

    coherComp = csa->coherComp;
    pending = (pendingRequest){0};
    countDown = 0;
    coherComp->registerCacheInterface(coherCallback);

    return self;
}

// Starter access behavior below is intentionally deferred to later phases.
// This routine is a linkage to the rest of the memory hierarchy
void coherCallback(int type, int procNum, int64_t addr)
{
    switch (type)
    {
        case NO_ACTION:
        case DATA_RECV:
            // TODO: check that the addr is the pending access
            // This indicates that the cache has received data from memory
            countDown = 1;
            break;

        case INVALIDATE:
            // This is taught later in the semester.
            break;

        default:
            break;
    }
}

void memoryRequest(trace_op* op, int processorNum, int64_t tag,
                   void (*callback)(int, int64_t))
{
    assert(op != NULL);
    assert(callback != NULL);

    // Simple model to only have one outstanding memory operation
    if (countDown != 0)
    {
        assert(pending.memCallback != NULL);
        pending.memCallback(pending.procNum, pending.tag);
    }

    pending = (pendingRequest){
        .tag = tag, .procNum = processorNum, .memCallback = callback};

    // In a real cache simulator, the delay is based
    // on whether the request is a hit or miss.
    countDown = 2;

    // Tell memory about this request
    // TODO: only do this if this is a miss
    // TODO: evictions will also need a call to memory with
    // invlReq(addr, procNum) -> true if waiting, false if proceed
    coherComp->permReq(false, op->memAddress, processorNum);
}

int tick()
{
    // Advance ticks in the coherence component.
    coherComp->si.tick();

    if (countDown == 1)
    {
        assert(pending.memCallback != NULL);
        pending.memCallback(pending.procNum, pending.tag);
    }

    return 1;
}

int finish(int outFd)
{
    return 0;
}

int destroy(void)
{
    cache_storage_destroy(&state);
    free(self);
    self = NULL;
    coherComp = NULL; /* Borrowed from the framework, never freed here. */
    pending = (pendingRequest){0};
    countDown = 0;
    return 0;
}
