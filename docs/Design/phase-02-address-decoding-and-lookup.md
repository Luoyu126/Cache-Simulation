# Phase 02: Address Decoding and Lookup

## Status

- Design: Confirmed
- Implementation: Implemented
- Acceptance: Accepted locally

## Scope and Non-Goals

Follow `phase-roadmap.md#phase-02-address-decoding-and-lookup`:

- Derive set index and tag from a 64-bit byte address. Block number may be a
  local intermediate; block start address and offset are deferred to consumers.
- Locate existing storage for a `(set, way)` pair.
- Search the selected set for a valid matching line.
- Return a matching line pointer on hit, or NULL on any miss. Choosing an
  invalid line or eviction victim belongs to the later placement path.

This phase only inspects addresses and cache state. It does not fill or evict
lines, update replacement/dirty metadata, issue coherence requests, or complete
processor requests. Those behaviors remain assigned to later phases.

## Dependencies and Relevant Code

- Assignment: `docs/346f26 P1-cache.pdf`, Section 3 (64-bit byte addresses,
  set/block configuration).
- Phase 01 design: `phase-01-initialization-and-lifecycle.md`.
- `teamCache/cache_internal.h`: `cache_state` retains s/E/b/S/B and nested
  line pointers; `cache_line` contains valid, tag, last-access, and dirty fields.
- `teamCache/lifecycle.c`: allocated lines start invalid with zero metadata.
  After 2026-09-17, unused sets are NULL until `cache_ensure_line` creates them.
- `teamCache/cache.c`: owns the private state object and framework entry points.

## Student Proposal and Reasoning

- The student explicitly requests an agent-proposed implementation for review,
  explaining that address-decoding background has already been covered.
- Agent proposal (not yet confirmed): compute offset, block number, block
  address, set index, and tag using unsigned 64-bit masks and sequential shifts;
  expose a decoded-address structure. Scan only the selected set, retaining a
  matching valid way and the first invalid way; do not stop at an invalid line.
  Return both way indices, with SIZE_MAX indicating absence, without mutating
  cache state. Proposed files are `lookup.h` and `lookup.c`.
- Student review: questions whether only set index, tag, and offset are needed.
  Review conclusion: these suffice as the decoding result for current lookup;
  block number can remain a local intermediate. Block start address is not
  needed to search for a hit and can be derived when later hierarchy/split
  handling needs it. The roadmap's five derived quantities do not require five
  persistent/output fields. Exact result fields remain Proposed pending review;
  no roadmap scope change or implementation has been made.
- Student asks where block start addresses will be used. Scope explanation:
  Phase 03 identifies blocks in hierarchy requests/completions; Phase 04 needs
  the evicted block's address (distinct from the incoming miss address); Phase
  05 operates on each block covered by a split access. `common/coherence.h`
  exposes address-based `permReq` and `invlReq`; the supplied coherence code
  keys its state by the passed address. These are future consumers, not a
  decision to store block addresses in every line or in Phase 02's result.
- Confirmed student correction: block start address must not be part of the
  current lookup function/result. Keep the interface focused on current lookup
  needs. This supersedes the five-field decoded-result proposal; compute block
  start addresses in later modules when needed. Roadmap scope is updated to
  remove the unnecessary Phase 02 block-address deliverable.
- Student review: asks whether lookup returns a line pointer and observes that
  offset is unnecessary for that responsibility. Review: identifying a resident
  line needs only set index and tag; same-block byte offsets identify the same
  line. Offset does not need to be computed or returned by a hit-only lookup.
- Rejected agent proposal: hit lookup returns a borrowed pointer to
  the valid matching line, or NULL on miss. Invalid-line discovery is a separate
  operation returning an invalid-line pointer or NULL if the set is full. A
  lookup miss must not be represented by returning an invalid line through the
  hit-only interface. Student rejects this split because a miss could require
  scanning the same set again to find space.
- Superseded student proposal: perform hit lookup and invalid-line discovery
  together. Return a matching line plus a "label" on hit; when no hit exists
  but an invalid line does, return an invalid line plus a label, with valid=0
  indicating available space. Return NULL only on a miss with a full set.
  Student motivation: avoid a second traversal. The student suggests that
  eviction could later be integrated; this remains a Phase 04 discussion.
- Historical review questions for the superseded proposal: does "label" mean the incoming address tag or
  a hit/empty status? If an invalid line precedes a valid matching line, when
  does traversal return? The full-set NULL condition must exclude a hit in a
  full set. No implementation or silent algorithm correction has been made.
- Confirmed student revision: lookup returns a line pointer only on a hit;
  every miss returns NULL. The later fill/placement path groups available-line
  selection with eviction when no invalid line exists. This supersedes the
  combined lookup result and resolves its extra-label questions as unnecessary.
  The earlier split-function suggestion was rejected at that time; hit-only
  lookup is now accepted, but a Phase 02 empty-line helper is not required.
- Terminology clarification: Phase 01 allocates every line, so an available
  slot is a non-NULL line with valid=0, not a NULL pointer. Detailed replacement
  timing and ownership of the placement module remain later-phase decisions.

## Open Design Questions

None blocking Phase 02. The student has reviewed/refined the lookup behavior
and requested its implementation. Placement and access-time mutations remain
later-phase decisions.

## Confirmed Decisions and Invariants

- Confirmed: lookup computes only the set index/tag needed for a hit query;
  no block-address/offset output or extra status label is needed.
- Confirmed: return a borrowed matching-line pointer on hit, NULL on miss.
  Do not return or select an invalid line or eviction victim in lookup.

Inherited organization: phase implementation belongs in separate C/header files
under `teamCache/`; `cache.c` stays the coordinating entry point.

Implementation translation of the accepted plan:

- Add `lookup.h` / `lookup.c` with
  `cache_line* cache_lookup(const cache_state* state, uint64_t addr)`.
- Precondition: state was successfully initialized and its storage is live.
- Derive block number locally, then set index/tag using the reviewed unsigned
  mask and sequential shifts. No decoded-result structure, offset, or block
  start address is introduced.
- Return the existing line pointer; ownership remains with the cache. The
  caller must not free it. The pointer permits future caller-owned updates,
  but lookup itself does not mutate any fields. The const state parameter
  protects direct state fields, not objects behind nested pointers; tests also
  verify that the line objects remain unchanged.
- Compile lookup into `teamCache`. Wiring it into `memoryRequest()` belongs to
  Phase 03 so the starter's timing behavior is not changed in this phase.

Roadmap requirements, not newly student-designed algorithms:

- Preserve the full 64-bit address information.
- A hit requires both validity and tag equality in the selected set.
- Lookup leaves all cache state unchanged.

## Confirmed Pseudocode

Faithful implementation of the reviewed decoding proposal and the student's
confirmed hit-only behavior:

```text
lookup(state, addr):
    block_number = addr >> b
    set_index = block_number & (S - 1)  [64-bit unsigned mask]
    tag = block_number >> s
    For each way in the selected set:
        line = sets[set_index][way]
        If line.valid and line.tag == tag:
            Return line
    Return NULL
```

No allocation, mutation, empty-line selection, or callback occurs.

## Acceptance Behaviors and Test Scenarios

Inherited roadmap expectations:

- With b=4, addresses 0x10 and 0x18 share tag/set identity and return the same
  resident line despite referring to different bytes within that block.
- Different tags mapping to one set remain distinguishable.
- s=0 models a single set correctly.
- High 64-bit addresses are not truncated or interpreted using signed arithmetic.
- Invalid lines cannot yield hits even when their tag fields match.
- All lookup operations preserve the stored metadata.

Agent-authored checks will exercise these existing acceptance requirements
with seeded line metadata and fixed expected addresses/pointers. They do not
introduce new replacement or fill behavior. Additional student-selected tests
can extend them later.

## Commands and Observed Results

### Passing checks

- `teamCache` target builds with the new module; the shared library exports
  `cache_lookup` and retains its lifecycle entry points.
- Strict C11 compilation of lookup, lifecycle, and the new harness passes
  `-Wall -Wextra -Wpedantic -Werror` without warnings.
- All **18 lookup checks** pass. Fixed expected pointers cover same-block
  addresses, wrong-set tags, invalid tag-zero lines, a hit after an invalid
  way, different tags in one set, misses with spare space and in a full set,
  a hit in a full set, high-bit addresses versus their low-32-bit counterparts,
  s=0, and b=10. Each query snapshots all state, line metadata, and nested
  pointers to verify no mutation.
- Valgrind reports **17 allocations, 17 frees, 0 bytes at exit, 0 errors**.
- Real engine loads the rebuilt component and runs the existing minimum-config
  empty trace: exit 0, `Ticks - 1` (with the framework's existing trailing NUL).
- `git diff --check` passes.

### Reproduction

From the repository root (reuse the Phase 01 build directory):

```sh
cmake -S . -B /tmp/cadss-phase01-build -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build /tmp/cadss-phase01-build --target teamCache -j 4
gcc -std=c11 -g -O0 -Wall -Wextra -Wpedantic -Werror -Icommon -IteamCache tests/teamCache/phase02_lookup_test.c teamCache/lookup.c teamCache/lifecycle.c -o /tmp/phase02_lookup_test
valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=99 /tmp/phase02_lookup_test
git diff --check
```

Engine smoke check uses the executable, targets, and temporary fixtures whose
creation is documented in the Phase 01 record:

```sh
cd /tmp/cadss-phase01-build
/home/chenyy/cadss_public/cadss-engine -c teamCache -s /tmp/cadss-phase01-build/phase01-minimum.config -t /tmp/cadss-phase01-build/phase01-empty.trace
```

### Limits

- Nonempty trace behavior is not part of this phase. `cache.c` is unchanged;
  Phase 03 will call lookup while implementing the actual request path.
- Full-project compilation was not rerun: the known missing `zlib.h` limitation
  remains recorded in Phase 01. The relevant component build succeeds.

## Implementation Mapping

- `teamCache/lookup.h`: borrowed-pointer contract and function declaration.
- `teamCache/lookup.c:cache_lookup`: unsigned address decoding and hit-only scan.
- `teamCache/CMakeLists.txt`: includes `lookup.c` in the shared-library target.
- `tests/teamCache/phase02_lookup_test.c`: seeded-state acceptance checks with
  fixed expectations and state-preservation snapshots; no fill implementation.

## Deviations and Follow-Ups

- Student-directed scope refinement: defer block start address calculation to
  its later consumers; `phase-roadmap.md` records the reason. The dirty-marker
  update semantics also remain outside this phase.
- Subsequent student revision: offset calculation and invalid-line selection
  are also removed from this phase; placement is discussed with fills/eviction.
