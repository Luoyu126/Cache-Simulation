# Phase 06: RRIP Replacement

## Status

- Design: Confirmed
- Implementation: Implemented
- Acceptance: Partial; focused reference timing matches, RRIP conflicts remain

The student asks what Phase 06 adds after Phase 05. This record captures the
scope and the design questions to resolve before implementation.

## Scope and Non-Goals

From the canonical roadmap, Phase 06 adds the RRIP replacement policy while
preserving the existing LRU behavior:

- Select RRIP with `-R <k>`; omitting `-R` keeps LRU as the default.
- Maintain a k-bit re-reference prediction value, RRPV, for each line.
- Apply the confirmed RRIP rules for hit updates, fill insertion, aging, and
  victim selection.
- Put replacement-policy choices behind a common boundary shared by LRU.

Request queueing, write buffering, victim cache, subblocking, and new split
semantics remain outside this phase.

## Relevant Specification and Code

- `docs/Design/phase-roadmap.md`: Phase 06 scope and acceptance behaviors.
- `docs/346f26 P1-cache.pdf`, page 2: RRIP definition and rules.
- `teamCache/lifecycle.c`: already parses `-R <k>` into
  `CACHE_POLICY_RRIP` and `state->rrip_bits`.
- `teamCache/cache_internal.h`: `cache_line.time_stamp` is the shared LRU
  timestamp or RRIP prediction value.
- `teamCache/replacement.h/.c`: centralized hit, fill, invalid placement, and
  full-set LRU/RRIP victim behavior.
- `teamCache/access.c`: reports hit and fill events to the replacement module.
- `teamCache/eviction.c`: obtains its target from the replacement module and
  retains the existing coherence ordering.

## Handout RRIP Rules

The handout describes static RRIP:

- Each cache line has a k-bit RRPV.
- On a cache hit, the accessed block's RRPV is reset to `0`.
- On a miss in a full set, choose the first block whose RRPV is `2^k - 1`.
- If no block has that maximum RRPV, increment every block's RRPV by one and
  repeat until a victim exists.
- On insertion, set the inserted block's RRPV to a nonzero value; the handout
  recommends `2^k - 2`.

These rules do not change the coherence timing contract: misses and evictions
still use the existing Phase 03/04 lower-hierarchy flow.

## Design Questions

- What helper boundary should own replacement policy behavior so LRU and RRIP
  do not stay embedded directly in `access.c` and `eviction.c`?
- Should invalid-line placement be shared by both policies before consulting
  a victim selector?
- What integer type and saturation rule should represent k-bit RRPV,
  especially for the already-parsed `k = 64` edge case?
- Exactly when should RRIP metadata update: on hit, on fill after `DATA_RECV`,
  and possibly when an invalid target is selected?
- Should LRU continue using `last_access` exactly as before, with RRIP leaving
  it unused, or should the replacement module hide both metadata styles?
- What focused traces or harness checks should demonstrate hit reset,
  recommended insertion value, aging, first-eligible victim selection, and
  unchanged LRU behavior?

## Current Incremental Meaning

Phase 06 is primarily a replacement-policy refactor and extension. Previous
phases determine whether a block hits, how misses wait for data, how evictions
coordinate with coherence, and how split accesses serialize their block work.
Phase 06 changes only the metadata and victim-choice policy used when a target
set is full, plus the metadata updates that happen on hits and fills.

## Student Proposal and Review

Current student proposal (`Proposed`):

- Keep the selected policy and RRIP width with the cache-wide state that
  already owns `s`, `E`, `b`, and `sets`.
- Add a top-level victim-selection wrapper that dispatches to separate LRU and
  RRIP implementations with the same interface.
- Represent each RRPV as a binary integer requiring at most 64 host bits for
  the currently accepted `k <= 64`.
- Explore whether LRU and RRIP can reuse the same per-line metadata field.

Confirmed student decisions:

- `Confirmed`: invalid lines are preferred before selecting any valid victim,
  for both LRU and RRIP.
- `Confirmed`: a newly filled RRIP line receives the recommended insertion
  value `2^k - 2`.
- `Confirmed`: LRU and RRIP share one `uint64_t` per-line replacement metadata
  field. Its meaning is selected by `cache_state.policy`, because one simulator
  run uses exactly one replacement policy.
- `Confirmed`: replacement-policy dispatch is centralized in a replacement
  module for hit updates, fill updates, and full-set victim selection. Callers
  report the replacement event rather than each independently branching on
  the configured policy.
- `Confirmed`: allow `k = 1` and apply the selected insertion formula exactly,
  yielding RRPV `0`. This deliberately follows `2^k - 2` even though it is the
  edge case that conflicts with the handout's general nonzero-insertion text.
- `Confirmed`: the RRIP aging loop first searches for a maximum-valued line and
  ages the set only when none exists. Consequently every value is at most
  `max - 1` on entry to aging, and one increment cannot wrap; no saturation
  case is reachable under this invariant, including when `k = 64`.

Review results:

- `cache_state.policy` and `cache_state.rrip_bits` already retain the parsed
  `-R <k>` selection; no new global option variable is needed.
- Dispatch is needed for more than victim selection. A hit and a fill also
  update policy metadata, so the proposed policy boundary must account for
  those events.
- A 64-bit integer can encode a k-bit RRPV; it does not allocate `2^k` storage.
  The `k = 64` maximum and insertion values must be computed without an
  undefined `1 << 64`.
- Reusing one metadata field does not make LRU and RRIP logically identical.
  Current LRU stores a globally increasing last-access timestamp and chooses
  the minimum. RRIP stores a bounded prediction value, resets hits to zero,
  inserts at maximum minus one, ages a whole set, and chooses the first
  maximum. A shared storage slot may be mechanically possible because only one
  policy is active, but its meaning would be policy-dependent and still
  requires separate update and victim-selection algorithms.

The centralized C interfaces are implemented as `cache_replacement_hit`,
`cache_replacement_fill`, and `cache_replacement_target`.

## Confirmed Pseudocode

This is a faithful organization of the student's confirmed decisions, not a
new algorithm choice:

```text
on hit(line):
    if policy is LRU:
        advance the existing access sequence
        store the new sequence in line replacement metadata
    else:
        store 0 in line replacement metadata

on fill(line):
    if policy is LRU:
        advance the existing access sequence
        store the new sequence in line replacement metadata
    else:
        store (2^k - 2) in line replacement metadata

choose placement target(set):
    scan ways in order and return the first invalid line, if any
    if policy is LRU:
        return the line with the smallest replacement metadata
        preserve the existing first-way tie behavior
    else:
        maximum = 2^k - 1
        repeat:
            scan ways in order and return the first line equal to maximum
            increment every line's replacement metadata by one
```

The RRIP increment cannot wrap under the confirmed loop invariant: aging runs
only after a complete scan proves that every value is below the maximum.
The implemented field name is the student-selected `time_stamp`.
`replacement.c` handles `k = 64` with `UINT64_MAX` instead of shifting by 64.

## Implementation Mapping

- `teamCache/cache_internal.h`: shared per-line `time_stamp` metadata.
- `teamCache/replacement.h/.c`: centralized LRU/RRIP metadata updates,
  invalid-line preference, aging, and victim selection.
- `teamCache/access.c`: enables RRIP accesses and distinguishes hit updates
  from fill initialization.
- `teamCache/eviction.c`: delegates placement and victim selection.
- `teamCache/CMakeLists.txt`: compiles the replacement module.
- `tests/teamCache/phase06_rrip_test.c`: integrated RRIP access flow and direct
  policy boundary tests.
- Phase 01--04 tests use the renamed `time_stamp` field without changing their
  original expectations.

## Commands and Observed Results

Passing checks:

```sh
cmake -S . -B /tmp/cadss-phase06-build -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build /tmp/cadss-phase06-build --target teamCache cadss-engine trace \
  processor branch coherence interconnect memory -j 4

gcc -std=c11 -g -O0 -Wall -Wextra -Wpedantic -Werror \
  -Icommon -IteamCache tests/teamCache/phase06_rrip_test.c \
  teamCache/cache.c teamCache/access.c teamCache/eviction.c \
  teamCache/replacement.c teamCache/split.c teamCache/lookup.c \
  teamCache/lifecycle.c -o /tmp/phase06_rrip_test
/tmp/phase06_rrip_test

valgrind --leak-check=full --show-leak-kinds=all \
  --errors-for-leak-kinds=all --error-exitcode=99 /tmp/phase06_rrip_test

# Run from /tmp/cadss-phase06-build so framework libraries resolve.
/home/chenyy/cadss_public/cadss-engine -v -s \
  /home/chenyy/cadss_public/ex_rrip.config -c teamCache -t \
  /home/chenyy/cadss_public/traces/cache/load.trace
/home/chenyy/cadss_public/cadss-engine -v -s \
  /home/chenyy/cadss_public/ex_rrip.config \
  -c /home/chenyy/cadss_public/refCache -t \
  /home/chenyy/cadss_public/traces/cache/load.trace

git diff --check
```

- The component and simulator framework targets build successfully.
- The Phase 06 harness passes hit reset, recommended fill value, invalid-line
  preference, aging, first-maximum selection, integrated eviction/refill,
  dirty replacement, unchanged LRU selection, and `k = 1`/`k = 64` boundaries.
- Phase 01, 02, 03, 04, and 05 regression harnesses all pass after linking
  `replacement.c`.
- Valgrind reports 30 allocations, 30 frees, zero bytes at exit, and zero
  errors.
- The real engine runs `traces/cache/load.trace` with `ex_rrip.config` and
  `teamCache`, completing in 316 ticks after the queue-first timing correction.
- IDE diagnostics and `git diff --check` report no errors.
- The prebuilt `refCache/librefCache.so` was run by passing its absolute
  component directory to the engine. On `traces/cache/load.trace` with
  `ex_rrip.config`, both implementations report the same sequence of three
  misses and six hits, and both take 316 ticks. This resolves the inherited
  Phase 03 hit-timing discrepancy for this RRIP trace. Acceptance remains
  Partial until a reference-comparable RRIP replacement-conflict trace is run.
