#include <cache.h>
#include <trace.h>

#include "lifecycle.h"
#include "access.h"

#include <stdio.h>
#include <stdlib.h>

/* The state object lives here; lifecycle.c only borrows its address. */
static cache_state state = {0};
static cache* self = NULL;
static coher* coherComp = NULL;

int processorCount = 1;
int CADSS_VERBOSE = 0;

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
    coherComp->registerCacheInterface(coherCallback);

    return self;
}

void coherCallback(int type, int procNum, int64_t addr)
{
    cache_access_event(&state, coherComp, type, procNum, (uint64_t)addr);
}

void memoryRequest(trace_op* op, int processorNum, int64_t tag,
                   void (*callback)(int, int64_t))
{
    cache_access_request(&state, coherComp, op, processorNum, tag, callback);
}

int tick(void)
{
    cache_access_tick(&state, coherComp);
    return 1;
}

int finish(int outFd)
{
    (void)outFd;
    return 0;
}

int destroy(void)
{
    cache_access_destroy(&state);
    cache_storage_destroy(&state);
    free(self);
    self = NULL;
    coherComp = NULL; /* Borrowed from the framework, never freed here. */
    return 0;
}
