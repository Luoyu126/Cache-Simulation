#include "lifecycle.h"

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static bool error(const char* message)
{
    fprintf(stderr, "teamCache: %s\n", message);
    return false;
}

/* Reject signs, whitespace, trailing text, and conversion overflow. */
static bool parse_number(const char* text, uintmax_t* value)
{
    if (text == NULL || *text == '\0')
        return false;
    for (const char* p = text; *p != '\0'; ++p)
        if (*p < '0' || *p > '9')
            return false;

    errno = 0;
    char* end;
    *value = strtoumax(text, &end, 10);
    return errno != ERANGE && *end == '\0';
}

static bool parse_options(cache_state* state, const cache_sim_args* args)
{
    if (args == NULL || args->arg_count < 1 || args->arg_list == NULL)
        return error("missing cache arguments (-s, -E, -b are required)");
    for (int i = 0; i < args->arg_count; ++i)
        if (args->arg_list[i] == NULL)
            return error("NULL entry in cache arguments");

    bool have_s = false, have_E = false, have_b = false;
    bool ok = false;
    int saved_optind = optind, saved_opterr = opterr, saved_optopt = optopt;
    char* saved_optarg = optarg;
    /* GNU getopt is shared by CADSS components; restart for this argument list. */
    optind = 0;
    opterr = 0;

    int option;
    while ((option = getopt(args->arg_count, args->arg_list,
                            "+:s:E:b:R:w:i:u:")) != -1)
    {
        uintmax_t value;
        if (option == '?' || option == ':')
        {
            error("unknown option or missing option value");
            goto done;
        }
        if (!parse_number(optarg, &value))
        {
            error("option values must be nonnegative decimal integers");
            goto done;
        }
        switch (option)
        {
            case 's':
                if (value >= 64 || value >= sizeof(size_t) * CHAR_BIT)
                    goto range_error;
                state->s = (unsigned int)value;
                have_s = true;
                break;
            case 'E':
                if (value == 0 || value > SIZE_MAX)
                    goto range_error;
                state->E = (size_t)value;
                have_E = true;
                break;
            case 'b':
                if (value < 4 || value > 10)
                    goto range_error;
                state->b = (unsigned int)value;
                have_b = true;
                break;
            case 'R':
                if (value < 1 || value > 64)
                    goto range_error;
                state->policy = CACHE_POLICY_RRIP;
                state->rrip_bits = (unsigned int)value;
                break;
            case 'w':
                if (value > 1)
                    goto range_error;
                state->write_buffer_mode = (unsigned int)value;
                break;
            case 'i':
            case 'u':
                if (value > (option == 'i' ? 8u : 10u))
                    goto range_error;
                if (value != 0)
                    fprintf(stderr, "teamCache: ignoring -%c %ju; feature is "
                            "outside the Fall 2026 scope\n", option, value);
                break;
        }
    }
    if (!have_s || !have_E || !have_b || optind != args->arg_count)
    {
        error("require -s, -E, -b and no positional arguments");
        goto done;
    }
    ok = true;
    goto done;

range_error:
    error("option value outside supported range");
done:
    optind = saved_optind;
    opterr = saved_opterr;
    optopt = saved_optopt;
    optarg = saved_optarg;
    return ok;
}

static bool derive_dimensions(cache_state* state)
{
    if (state->s + state->b > 64)
        return error("set and block bits exceed the 64-bit address width");

    state->S = (size_t)1 << state->s;
    state->B = (size_t)1 << state->b;

    if (state->S > SIZE_MAX / sizeof(*state->sets)
        || state->E > SIZE_MAX / state->S)
        return error("cache dimensions overflow host storage size");

    size_t line_count = state->S * state->E;
    size_t outer_bytes = state->S * sizeof(*state->sets);
    size_t bytes_per_line = sizeof(cache_line*) + sizeof(cache_line);
    if (line_count > (SIZE_MAX - outer_bytes) / bytes_per_line)
        return error("cache allocation size overflows size_t");
    return true;
}

static cache_line** ensure_set(cache_state* state, size_t set_index)
{
    if (state->sets[set_index] == NULL)
    {
        state->sets[set_index] = calloc(state->E, sizeof(*state->sets[set_index]));
        if (state->sets[set_index] == NULL)
            return NULL;
    }
    return state->sets[set_index];
}

cache_line* cache_ensure_line(cache_state* state, size_t set_index, size_t way)
{
    if (state == NULL || state->sets == NULL || set_index >= state->S
        || way >= state->E)
        return NULL;
    cache_line** set = ensure_set(state, set_index);
    if (set == NULL)
        return NULL;
    if (set[way] == NULL)
    {
        set[way] = calloc(1, sizeof(*set[way]));
        if (set[way] == NULL)
            return NULL;
    }
    return set[way];
}

void cache_storage_destroy(cache_state* state)
{
    if (state == NULL)
        return;
    if (state->sets != NULL)
    {
        for (size_t set = 0; set < state->S; ++set)
        {
            if (state->sets[set] == NULL)
                continue;
            for (size_t way = 0; way < state->E; ++way)
                free(state->sets[set][way]);
            free(state->sets[set]);
        }
        free(state->sets);
    }
    *state = (cache_state){0};
}

bool cache_storage_init(cache_state* state, const cache_sim_args* args)
{
    if (state == NULL || state->sets != NULL)
        return error("storage init requires an empty state object");
    *state = (cache_state){0}; /* LRU, no write buffering. */
    if (!parse_options(state, args) || !derive_dimensions(state))
        goto fail;

    state->sets = calloc(state->S, sizeof(*state->sets));
    if (state->sets == NULL)
        goto allocation_failed;
    return true;

allocation_failed:
    error("cache allocation failed");
fail:
    cache_storage_destroy(state);
    return false;
}
