# Team Cache Development Phase Roadmap

## Purpose

This document is the canonical implementation map for `teamCache`. It defines
the boundary, dependencies, and observable acceptance behavior of each
development phase. Detailed design decisions belong in the dedicated phase
documents listed below.

The project is divided into Phase 0 plus eight implementation phases and one
final validation/experimentation phase. A phase is not accepted merely because
its code compiles: its required observable behaviors must also pass.

## Project Boundaries

- Primary implementation directory: `teamCache/`
- Reference implementation: `refCache/librefCache.so`
- Runtime selection: `cadss-engine -c teamCache` or `-c refCache`
- Required policies/features: LRU, RRIP, variable access sizes, split-line
  accesses, request queuing, and single-entry write buffering (`-w 1`)
- Fall 2026 non-goals unless the roadmap is explicitly revised: victim cache
  and subblocking
- A cache hit or completed request must notify the processor through the
  supplied callback at the required tick boundary.
- Cache misses and evictions interact with the memory hierarchy only through
  the provided coherence interface.

## Code Organization

- Confirmed student instruction during Phase 01: place each phase's code in
  separate C files or subdirectories under `teamCache/`. Keep `cache.c` concise
  by calling high-level internal interfaces.
- Reason: make implementation layers easy to read and keep the framework entry
  file small. Record concrete filenames and interface decisions in each phase's
  design document. This organization update does not change phase scope or
  acceptance requirements.

## Phase Map

```text
Phase 00  Project Scaffold                              [Implemented]
    |
Phase 01  Initialization and Lifecycle                 [Accepted locally]
    |
Phase 02  Address Decoding and Lookup                  [Accepted locally]
    |
Phase 03  LRU Hits, Misses, and Fills
    |
Phase 04  Eviction and Dirty Write-Back
    |
Phase 05  Variable-Size and Split-Line Accesses
    |
Phase 06  RRIP Replacement
    |
Phase 07  Request Queueing
    |
Phase 08  Single-Entry Write Buffer
    |
Phase 09  Reference Validation and Experiments
```

## Phase 00: Project Scaffold

Dedicated design file: `phase-00-project-scaffold.md`

### Scope

- Create `teamCache/` from the framework cache starter.
- Register the subdirectory in the root CMake build.
- Name the shared-library target `teamCache`.
- Point `submission` at `teamCache`.

### Expected acceptance behavior

- The `teamCache` target builds and produces `libteamCache.so`.
- The engine can resolve the component using `-c teamCache` once the required
  framework targets are built.
- `submission` contains exactly `__cache__:teamCache`.
- The original `cache/` starter and `refCache/` remain unmodified.

### Current status

Implemented and target-build-checked. Details and remaining environment-level
validation are recorded in `phase-00-project-scaffold.md`.

## Phase 01: Initialization and Lifecycle

Dedicated design file: `phase-01-initialization-and-lifecycle.md`

### Scope

- Define cache-line and simulator-owned state.
- Parse and validate cache parameters needed by later phases.
- Compute set count, block size, and line count safely.
- Dynamically allocate and zero-initialize cache storage.
- Initialize the public cache interface and register the coherence callback.
- Release all simulator-owned allocations in `destroy()` without freeing
  framework-owned components.

### Non-goals

- Performing cache accesses.
- Issuing coherence requests.
- Implementing a replacement decision.

### Expected acceptance behavior

- Valid `-s`, `-E`, and `-b` configurations initialize successfully.
- Default policy state is LRU; optional future-phase parameters can be parsed
  without corrupting initialization state.
- Invalid or overflowing dimensions fail cleanly instead of producing an
  invalid allocation or undefined shift.
- Every line begins invalid, clean, and with reset replacement metadata.
- An empty trace initializes and destroys the component without a crash,
  assertion failure, or simulator-owned memory leak.
- Repeated simulator processes with different valid configurations do not
  depend on stale state.

### Current status

Implemented and locally accepted: lifecycle harness, all 10 injected allocation
failures, component Valgrind checks, and four real-engine empty-trace processes
pass. Full-project build remains limited by missing `zlib.h`; whole-engine
framework leaks are distinct from the verified component-owned cleanup.
Commands and details are in `phase-01-initialization-and-lifecycle.md`.

## Phase 02: Address Decoding and Lookup

Dedicated design file: `phase-02-address-decoding-and-lookup.md`

### Scope

- Derive set index and tag from a 64-bit byte address. Block number may be a
  local intermediate; block start address and offset are not lookup outputs.
- Map a `(set, way)` pair to the allocated line storage.
- Search only the selected set for a valid matching tag.
- Return a borrowed matching-line pointer on hit, or NULL on miss.

### Non-goals

- Mutating replacement metadata as part of an actual access.
- Issuing miss requests or processor callbacks.

### Expected acceptance behavior

- With `b=4`, `0x10` and `0x18` have the same tag/set identity and find the
  same resident line despite their different positions within the block.
- Addresses mapping to the same set but different tags are distinguished.
- `s=0` correctly models a single set.
- High 64-bit addresses are decoded without truncation or signed arithmetic.
- Lookup returns a hit only when both valid and tag match in the selected set.
- Lookup does not modify cache contents or select an invalid line/victim.

### Current status

Implemented in `teamCache/lookup.c` and locally accepted: all 18 seeded-state
lookup checks pass, including read-only snapshots and high-address cases;
Valgrind reports no errors or leaks. The rebuilt library loads in the real
engine for an empty trace. Request-path integration belongs to Phase 03.
See `phase-02-address-decoding-and-lookup.md` for commands and evidence.

### Scope refinement

During Phase 02 design review, the student requested keeping block start
addresses out of lookup because they are not needed for its current task.
Their calculation is deferred to later hierarchy/eviction/split-access modules
when needed. This replaces the original five-quantity decoding deliverable;
same-block relationships remain part of decoding acceptance.

The student subsequently confirmed hit-only lookup returning a line pointer or
NULL. Offset computation is deferred to its consumers; invalid-line selection
moves to the fill path in Phase 03, with full-set eviction added in Phase 04.
Reason: keep lookup responsible only for locating an already-resident block
and keep placement choices together. No placement timing algorithm is implied.

## Phase 03: LRU Hits, Misses, and Fills

Dedicated design file: `phase-03-lru-hits-misses-and-fills.md`

### Scope

- Copy incoming request fields because the processor frees the supplied
  `trace_op` after `memoryRequest()` returns.
- Process one non-split foreground request at a time.
- Implement cache hits and LRU metadata updates.
- On a miss with an available invalid line, request permission/data through
  coherence, wait for completion, and install the block.
- Find the available invalid line in this phase's placement path, separately
  from hit lookup. Full-set victim selection remains Phase 04.
- Notify the processor exactly once at the required later tick.

### Non-goals

- Evicting a valid line from a full set.
- Split-line accesses, RRIP, or write buffering.

### Expected acceptance behavior

- A first access to an absent block misses and obtains data through coherence.
- A later access to the resident block hits without a DRAM fetch.
- Addresses within the same block reuse the resident line.
- A store to a resident line updates the required dirty state.
- No request callback occurs synchronously inside the original processor call.
- Each accepted request produces exactly one callback with the original
  processor number and request identifier.
- Supported no-eviction traces match `refCache` in verbose classification and
  total ticks.

### Current status

Implemented for the confirmed normal path. Strict compilation, metadata/timing
harness, component Valgrind, and Phase 01/02 regression checks pass. Five focused
traces match reference hit/miss classification, but traces with hits take one
fewer tick per hit than the reference. Reference acceptance remains pending;
see the Phase 03 record before changing the student-confirmed timing.

## Phase 04: Eviction and Dirty Write-Back

Dedicated design file: `phase-04-eviction-and-dirty-write-back.md`

### Scope

- Select the least-recently-used line when a target set is full.
- Preserve enough victim information to reconstruct its block address.
- Coordinate eviction/flush through `invlReq()`.
- Wait when the coherence layer reports an asynchronous eviction.
- Continue the original miss only after eviction requirements complete.
- Preserve correct clean and dirty behavior while replacing a line.

### Expected acceptance behavior

- An invalid line is preferred over evicting a valid line.
- A full set evicts the correct LRU line with deterministic tie behavior.
- A clean eviction and a dirty eviction follow the expected coherence path.
- The victim is not overwritten before an outstanding eviction completes.
- The original miss address and callback survive an asynchronous eviction.
- The replacement block becomes resident only after its data/permission is
  available.
- Targeted conflict traces and applicable local traces match `refCache` in
  classifications and total ticks.

### Current status

Implemented and locally checked: set-local LRU, delayed/immediate eviction,
victim preservation, refill ordering, and pending-request cleanup pass. Clean
and dirty conflict traces match reference at 506 ticks; mixed hit/eviction
classification matches with only the inherited Phase 03 hit-timing difference.
Full reference acceptance remains pending that clarification. Details and
reproduction commands are in the Phase 04 design record.

## Phase 05: Variable-Size and Split-Line Accesses

Dedicated design file: `phase-05-variable-size-and-split-line-accesses.md`

### Scope

- Respect the address and size of each memory operation.
- Detect whether the inclusive access range crosses a cache-line boundary.
- Process the lower-address block before the higher-address block.
- Complete the processor request only after every involved block access
  completes.

### Expected acceptance behavior

- Aligned and unaligned 1-, 2-, 4-, and 8-byte accesses that remain within a
  block perform one block lookup.
- An access such as `L 0x1f,2` with 16-byte blocks performs accesses to block
  `0x10` and then block `0x20`.
- Both halves independently produce the required hit, miss, and eviction
  effects.
- The processor receives one callback for the original request, not one per
  block.
- End-address calculation rejects or safely handles 64-bit overflow.
- Boundary-focused traces match `refCache` in event ordering and total ticks.

## Phase 06: RRIP Replacement

Dedicated design file: `phase-06-rrip-replacement.md`

### Scope

- Select RRIP through `-R <k>` while retaining LRU as the default.
- Maintain a k-bit RRPV for each line.
- Apply the confirmed hit, insertion, aging, and victim-selection rules.
- Keep replacement-policy decisions behind a boundary shared with LRU.

### Expected acceptance behavior

- Omitting `-R` preserves Phase 03/04 LRU behavior.
- RRIP metadata remains within its configured k-bit range.
- A hit updates the accessed line according to the confirmed RRIP rule.
- A fill initializes the line according to the confirmed insertion rule.
- When no immediately eligible victim exists, aging eventually produces one
  without wrapping metadata.
- Victim selection is deterministic where the specification requires the
  first eligible line.
- RRIP conflict traces match `refCache` in classifications and total ticks for
  valid comparable configurations.

## Phase 07: Request Queueing

Dedicated design file: `phase-07-request-queueing.md`

### Scope

- Accept and retain a second request while another foreground request is
  outstanding.
- Define request ownership and queue head/tail invariants.
- Start the next queued request only at the confirmed transition point.
- Preserve processor number, request identifier, operation, address, size,
  callback, and split-progress state for every queued request.

### Expected acceptance behavior

- Requests arriving while the cache is busy are queued rather than completing
  the active request early or overwriting its state.
- Requests are processed in the confirmed order.
- Every request is completed exactly once with its own original callback data.
- Queue transitions do not introduce an unintended same-tick callback.
- Destroying the simulator releases queued request storage.
- Multi-request targeted tests complete without loss, duplication, deadlock,
  or state leakage.

## Phase 08: Single-Entry Write Buffer

Dedicated design file: `phase-08-single-entry-write-buffer.md`

### Scope

- Implement the required `-w 1` behavior separately from the foreground
  request state.
- Buffer one eligible write miss and advance its lower-hierarchy work in the
  background.
- Allow only the independent foreground behavior confirmed by the design.
- Define behavior when the buffer is occupied or the next access misses.
- Prevent ineligible/unaligned writes from using the buffer.

### Expected acceptance behavior

- `-w 0` preserves all accepted non-buffered behavior.
- One eligible write miss can occupy the buffer without losing its address or
  coherence completion.
- Permitted independent hits progress while the buffered write is active.
- A second miss or another write requiring the occupied buffer waits according
  to the confirmed ordering rules.
- Unaligned accesses do not take the buffered fast path.
- Foreground and background operations cannot overwrite each other's callback
  or state.
- `wb-test.trace` and additional focused traces match `refCache` in total ticks
  and verbose behavior.

## Phase 09: Reference Validation and Experiments

Dedicated design file: `phase-09-reference-validation-and-experiments.md`

### Scope

- Run systematic differential checks between `teamCache` and `refCache`.
- Exercise all required policies, access sizes, and state transitions.
- Run sanitizer/leak checks where the environment supports them.
- Search cache configurations under the assignment's 54KB modeled-storage
  budget.
- Produce reproducible tables and results for the report.

### Expected acceptance behavior

- A clean checkout builds with the assignment-required build procedure on the
  supported course environment.
- All local traces and focused phase traces complete without crash, assertion,
  deadlock, lost callback, or duplicate callback.
- Supported configurations match `refCache` in verbose event order and total
  ticks, or every known discrepancy has a documented minimal reproduction and
  specification-based resolution.
- Simulator-owned allocations pass the selected memory-safety checks.
- The modeled storage calculation includes data, tags, valid/dirty state,
  replacement metadata, and required write-buffer overhead.
- Every official experiment trace is run across the documented candidate
  configurations.
- One configuration satisfying the 54KB budget is selected for all experiment
  traces using the documented AAT comparison method.
- Commands, configuration files, raw results, derived tables, and report claims
  are mutually reproducible.

## Phase Document Template

Each dedicated phase document should use the following structure as applicable:

```markdown
# Phase NN: Exact Phase Name

## Status
- Design: Open | Proposed | Confirmed
- Implementation: Not started | In progress | Implemented
- Acceptance: Not run | Partial | Accepted

## Scope and Non-Goals
## Relevant Specification and Code
## Dependencies
## Open Design Questions
## Student Proposal and Reasoning
## Confirmed Decisions and Rationale
## Invariants
## Interfaces and State
## Confirmed Pseudocode
## Counterexamples and Review Notes
## Acceptance Behaviors and Test Scenarios
## Implementation Mapping
## Commands and Observed Results
## Deviations, Follow-Ups, and Superseded Decisions
```

Status labels must reflect evidence. Agent suggestions remain `Proposed` until
the student confirms them; implemented behavior remains distinct from accepted
behavior until the required checks pass.
