# Phase 05: Variable-Size and Split-Line Accesses

## Status

- Design: Confirmed for the serial split-access contract
- Implementation: Implemented
- Acceptance: Partial; reference split-eviction timing differs

The student is preparing to develop Phase 05 and asks what its new features
mean relative to previous phases. This exchange establishes scope and explains
requirements. The subsequent student proposal is recorded below.

## Scope and Non-Goals

From the canonical phase roadmap:

- Respect the byte address and size of each memory operation.
- Detect whether its inclusive byte range crosses a cache-line boundary.
- Access the lower-address block before the higher-address block.
- Complete the original processor request only after all involved block
  accesses complete, with one original processor callback.

RRIP, queueing additional processor requests (Phase 07), write buffering,
victim caches, and subblocking are outside this phase. A queue holding the
parts of one original request is within the proposed Phase 05 scope. Cache block size remains a configuration parameter;
variable access size describes the processor operation, not variable line size.
The simulator tracks metadata and timing, not actual payload bytes.

## Relevant Specification and Code

- `docs/Design/phase-roadmap.md`, Phase 05: scope and acceptance requirements.
- `docs/346f26 P1-cache.pdf`, page 3: lower address is accessed first when an
  operation spans two cache lines; page 7: variable access sizes are evaluated.
- `common/trace.h`: `trace_op.memAddress`, `trace_op.size`, load/store kind.
- `common/cache.h`: original processor request and completion interface.
- `teamCache/access.h`: owned request copy, callback identity, current status.
- `teamCache/access.c`: currently accepts positive-sized requests contained
  within one block and rejects split-line requests explicitly; lookup, fetch,
  event matching, and completion currently use the single-request path.
- `teamCache/eviction.c`: target-set LRU eviction and coherence completion.

## Dependencies

- Phase 01: configured block size and cache-owned storage.
- Phase 02: read-only lookup of one block from a byte address.
- Phase 03: hits, misses, fills, LRU/dirty updates, retained request identity,
  asynchronous completion through framework ticks.
- Phase 04: set-local eviction and continued miss processing after eviction.

The inherited Phase 03 reference hit-timing discrepancy remains unresolved;
Phase 05 must track it separately when assessing timing acceptance.

## Requirements Explained

With 16-byte blocks, a load starting at `0x14` with size 4 covers
`0x14..0x17`, all inside block `0x10..0x1f`. A load at `0x1f` with size 2
covers `0x1f` and `0x20`, involving blocks starting at `0x10` and `0x20`.
The latter remains one processor operation despite requiring two block
accesses. Each block can have its own hit, miss, or eviction effects.
Unaligned access does not necessarily imply a cache-line crossing.

## Open Design Questions

- How should original request identity and progress through its involved
  blocks be represented and owned?
- What event starts processing the next block, including when the current
  block hits, waits for data, or requires eviction?
- How does completion of a block differ from completion of the processor
  request in the tick/callback flow?
- What supported size/block-size assumptions bound the number of involved
  blocks? The existing configuration must not silently imply a two-block cap.
- How should invalid sizes and an address range overflowing 64 bits be handled?

## Student Proposal and Confirmed Pseudocode

Student proposal (Proposed):

- Add an initial layer in memoryRequest to determine how many blocks the
  incoming byte range spans.
- Enqueue per-block work with its original request tag, total block count,
  and current block ordinal, regardless of hit/miss/eviction outcome.
- In tick, notify the processor once only after all blocks belonging to the
  original request have completed.

Confirmed behavioral intent: all parts must finish before one original
processor callback. No detailed scheduling pseudocode is confirmed yet.

Review findings and remaining questions:

- `cache_request` already retains `processor`, `request_tag`, and `callback`;
  request_tag is distinct from the cache-line address tag.
- A queue of parts of one original request can belong to Phase 05. Acceptance
  and scheduling of additional processor requests remains Phase 07. The
  student's intended queue scope and ownership need clarification.
- Enqueuing parts does not specify when each part starts. Consider the lower
  block waiting for eviction while the higher block is resident: when does
  the higher-block lookup occur, and what event permits it?
- Total count and ordinal identify position but do not alone establish that
  all prior parts completed if work is allowed to overlap. The student's
  scheduling rule must make completion inference valid.
- Exact block-count arithmetic, overflow behavior, storage lifetime, and the
  next-block tick transition remain Open. No implementation was changed.

## Acceptance Behaviors and Test Scenarios

Inherited from the roadmap, not newly selected student test cases:

- Aligned and unaligned 1-, 2-, 4-, and 8-byte accesses contained in a block
  perform one block lookup.
- `L 0x1f,2` with 16-byte blocks accesses block `0x10` before block `0x20`.
- Each involved block produces the required hit/miss/eviction effects.
- Only one callback completes the original processor request.
- End-address arithmetic rejects or safely handles 64-bit overflow.
- Boundary traces agree with reference event order and ticks, accounting
  explicitly for the inherited unresolved hit-timing discrepancy.

Student-selected concrete tests and expected timing remain Open.

## Commands and Observed Results

- Read roadmap, prior access/eviction records, relevant headers and source.
- Extracted relevant local handout pages with `python3` and `pypdf`.
- No implementation edits or test runs during this scope explanation.

## Callback Contract Clarification

The student asks whether the handout specifies how to reply when one part hits
and the other misses. Re-read handout pages 2–4 and the actual processor callback.

- Explicit text, page 2: a hit or complete request notifies the processor via
  callback on the next tick.
- Explicit text, page 3: access the lower address first for a two-line access.
- Explicit text, page 4: callback identifies completion of the memory request
  bearing the original request tag. The PDF shows callback(tag); the actual
  `common/cache.h` interface uses callback(processorNum, requestTag).
- Interface-derived interpretation: a mixed hit/miss operation remains one
  processor request; completion waits for all its parts and notifies once.
  The callback has no hit/miss classification, data, or latency argument.
  `processor/processor.c:memOpCallback` clears pendingMem for the completed
  request; notifying for only one part would release the processor too early.
- The PDF does not explicitly enumerate mixed hit/miss cases or specify the
  exact internal tick at which the second block lookup begins. Do not present
  a particular split scheduling algorithm or a total tick formula as quoted
  handout requirements. Existing hit-timing uncertainty remains separate.

This is specification/interface clarification, not confirmation of a student
state representation or split scheduling design. No implementation was changed.

## Existing Pending Storage Clarification

The student asks what concrete queue already holds misses in earlier phases.
Source inspection confirms that teamCache currently has no request queue:
`cache_state.active` owns one heap-allocated `cache_request`. Hit, data wait,
and eviction wait retain the same object and change its status. Events access
that object through active; tick detaches, notifies, and frees it when READY.
An overlapping processor request is explicitly rejected with the Phase 07
queueing diagnostic. Waiting across ticks does not itself require enqueueing.

The supplied interconnect separately has a linked queue of `bus_req` objects
(`queuedRequests`, `next`, enqBusRequest/deqBusRequest). These represent lower
bus transactions, not teamCache's original processor request or split parts.
They do not supply a teamCache request queue or original callback aggregation.

The agent's preceding queue discussion referred to the student's proposed
Phase 05 container, not an already implemented container. The student has not
yet confirmed whether to retain that proposed representation after clarification.
No implementation or scheduling decision changed in this exchange.

## Array and Completion-Table Proposal

Student revision (Proposed): store block requests in an array of
`cache_request*`; process any READY entry. Maintain a table indexed/keyed by
original request_tag with total block count and completed block count; notify
only when the counts match.

Review: aggregation can express the intended all-parts completion condition,
but correctness still depends on counting each block exactly once and on the
meaning of READY versus lower-level callbacks. A hit has no DATA_RECV, and an
eviction-complete event is not completion of the incoming block. Repeated tick
visits to READY must not inflate completion counts. Original processor and
callback ownership must survive part cleanup.

Scheduling remains Open: the array alone does not establish lower-address-first
access or authorize concurrent block processing. Ask whether "any READY entry"
means only collecting completed parts or starting all parts independently; a
lower block waiting for eviction and a resident higher block exposes this
ambiguity. Handout does not specify the precise second-block tick transition;
do not claim it explicitly prohibits every form of internal overlap.

Representation remains Open: request_tag is a 64-bit identifier, not a promised
compact array index (the current processor encodes its single-processor tags as
0, 256, 512, ...). Clarify direct indexing versus keyed lookup before allocating
a tag table. Multiple original processor requests remain Phase 07 scope.
No implementation changes; no detailed scheduling pseudocode confirmed.


## Serial Queue Revision

Latest student revision: maintain a queue of block requests, take one head
entry as the existing `cache_request* active`, process it, and retain the
original-tag total/completed-count aggregation. This supersedes the previous
array/any-READY scheduling proposal; that history remains above.

Confirmed direction: one active block at a time, pending blocks held in a
queue, one original callback after all parts complete. Review: this resolves
independent block advancement provided entries are ordered by increasing block
address and active is retained until its block completes, including eviction
and data waits. Queue ordering should be explicit in the detailed pseudocode.

Remaining Open: the exact tick/event for retiring a completed active block and
starting the next one; prevent same-tick completion of a newly started hit;
exactly-once completion counting and original callback ownership; tag-keyed
lookup versus raw tag array indexing; split arithmetic, allocation and cleanup.
The counting approach remains valid, although Phase 05 has only one original
processor request outstanding and does not inherently require a multi-tag map.
No representation simplification is selected on the student's behalf.

Next review question: when tick retires completed block A, when should block
B begin lookup? Internal transition timing is not settled by choosing a queue.
Implementation remains not started pending sufficiently detailed behavior.

## Same-Tick Next-Block Start

Confirmed student decision: when tick retires completed block A and increments
its original request's completed count, the vacant active slot can immediately
receive the next queued block B and begin lookup in that same tick. There is
no intentionally idle tick between A retirement and B lookup. This resolves
next-block start timing in the serial queue revision above.

Review: this is compatible with retaining one active block at a time; slot
availability permits starting work but does not establish same-tick completion.
Current access_tick completes the request READY on entry before ticking
coherence. Under inherited timing, newly started B becoming READY on a hit
must not be repeatedly retired by a drain-until-empty loop in the same tick.
Ask the student to confirm that B's retirement/counting waits for the next
tick, keeping the existing timing contract pending the separate reference
hit-timing clarification. This is a review question, not a newly confirmed
completion rule. Data received during the lower tick also retains the existing
later completion boundary. No implementation was changed.

## Confirmed Next-Block Completion Boundary

The student confirms the preceding timing example: tick T retires A and
increments its completion count, then starts B immediately; if B hits and
becomes READY in T, B is retired and counted in T+1, not T. This resolves the
review question above. A drain-until-empty loop must not retire newly READY
blocks in the same tick. Original-request notification still occurs once,
when all its blocks have been retired and counted. The inherited reference
hit-timing discrepancy remains a separate follow-up.

Faithful summary of the confirmed split transition:

```text
when tick retires completed active block A:
    count A once toward its original request
    if all blocks of that original request have completed:
        notify its processor once using the original completion identity
    otherwise:
        take the next queued block as active and begin lookup this tick
        if it becomes READY now, leave its retirement for the next tick
```

Split-range arithmetic, concrete queue and aggregation storage, original
callback ownership, and allocation/cleanup details still need specification.
No implementation changes in this confirmation exchange.

## Endpoint Calculation Proposal

Student proposal (Proposed): identify the block containing addr and the block
containing addr + size to delimit the involved blocks.

Review concern: addr + size denotes the exclusive byte-range boundary, so
including its containing block may add an untouched block when the boundary
is block-aligned. Counterexample for student review: B=16, addr=0x10, size=16;
proposed endpoints lie in blocks starting at 0x10 and 0x20. Ask the student
whether byte 0x20 is actually accessed and how they want to revise the endpoint.
No corrected endpoint formula has been confirmed or implemented. Overflow
handling remains Open for the eventual chosen arithmetic.

## Confirmed Inclusive Endpoint

The student corrects the endpoint by subtracting one. Confirmed mathematical
range for a positive size and representable addresses: addr through
addr + size - 1, inclusive. Use the blocks containing these endpoints to
bound the access. This supersedes the previous inclusive use of addr + size.
The exact safe machine arithmetic and overflow policy remain Open; the
formula is not permission to let an invalid range wrap around to zero.
Review case: addr=UINT64_MAX, size=2 exceeds the address space. Ask the student
what behavior the simulator should provide for such a request. No code changed.

## Confirmed Overflow Policy

The student approves diagnosing an access range exceeding UINT64_MAX and
terminating the simulator. This resolves the overflow-policy question above.
Preserve the existing rejection of size <= 0. Validate the inclusive range
before issuing block accesses; do not interpret a wrapped address as valid.
Safe C arithmetic will translate the confirmed range and rejection behavior.
No implementation changes in this exchange.

Remaining design work includes the concrete pending-block queue and original
request aggregation storage/ownership. In particular, the original processor,
request tag, and callback must survive until all block requests are retired;
the student has not yet specified where that completion record is owned.

## Confirmed Completion Identity Ownership

The student specifies that every per-block cache_request retains the original
processor, request_tag, and callback. When retiring the block that makes the
completed count equal the total, read those fields from that still-live block
request and notify the processor once. Earlier block objects may already have
been freed. A separate parent callback record is unnecessary for this chosen
representation; the aggregation record only needs the chosen count information.

Review: this resolves completion identity ownership provided every split copy
preserves identical original completion fields and those fields are consumed
before freeing the final block object (or copied to locals before freeing).
This is routine lifetime translation of the student's choice. Keep original
request_tag distinct from the block address/tag. Non-final block retirement
counts completion but does not invoke the processor callback. No implementation
changes in this exchange. Concrete queue/count-table representation remains
Open; this decision does not replace the count-based completion condition.


## Implementation Contract (Current; Supersedes Historical Open Items)

The student authorizes implementation, testing, and remote synchronization of
this design. Confirmed behavior is recorded across the exchanges above.

- Copy each original operation into per-block requests in ascending address
  order, spanning the blocks containing addr and addr + size - 1.
- Reject nonpositive size and address overflow before starting any access.
- Retain one active block; queued blocks perform no lookup or mutation until
  activated. The initial block starts inside memoryRequest as in Phase 03.
- Preserve each block's original processor, request_tag, and callback.
- Retain total/completed counters keyed by the original request identity.
- Tick retires only the block READY on entry and counts it once. On non-final
  completion, activate the next queued block immediately in the same tick.
  A newly READY hit is not retired again until the next tick.
- All-block completion calls the final block's original callback once. Preserve
  identity until it is consumed; free each retired block and the final counters.
- Coherence progress remains after the retirement/activation step. Eviction
  completion starts fetch, not block retirement or processor notification.
- Destroy releases active, queued requests, and counters before cache storage.
- Phase 07 multi-original-request acceptance remains excluded.

Concrete translation choices (agent mechanics, not additional student policy):

- Add split.h/.c for building/owning the FIFO and count record. A next pointer
  in cache_request and head/tail pointers in cache_state implement the chosen
  queue. Blocks also retain their zero-based ordinal for consistency checks.
- Use a one-entry keyed count table because only one original request can be
  outstanding in this phase; explicitly match processor and original tag.
  It is an allocated record with total/completed values, never raw tag indexing.
- Each block's op stores the intersection of the original byte range with
  that block (first address and byte count); all other operation fields are
  copied. Count blocks using the already-confirmed Phase 02 block-number
  decoding; enumerate through the last block without incrementing beyond it.
- Validate size - 1 against UINT64_MAX - addr before addition. Build the whole
  queue before starting access; clean partial allocation on failure and issue
  an error using the existing process-termination convention.
- access.c continues to own hit/miss/fill/eviction orchestration, splitting
  initial request acceptance from starting one active block. cache.c stays small.

Faithful combined pseudocode:

```text
memoryRequest:
    validate request, idle state, positive size and inclusive endpoint
    retain total/completed record and ascending per-block request copies
    take queue head as active and run existing single-block access logic
coherence event:
    use active block's existing eviction/data transitions
    data fill marks active READY; eviction completion only starts its fetch
tick:
    if active was READY on entry:
        detach that block and count it once
        if completed equals total:
            remove completed count record
            call detached block's original processor callback
        else:
            take next queue head as active and start it immediately
        free detached block
    tick coherence once
    leave any newly READY block until a later tick
destroy:
    free active, remaining queue entries and count record
    continue existing line/public-interface cleanup
```

Tests implement the student's chosen ordering, completion count, same-tick start
and later-hit retirement, identity lifetime, and overflow rejection. Agent
fixture choices exercise mixed hit/miss orders, dirty split stores, contained
1/2/4/8-byte accesses, a three-block generalization, and cleanup. These are
disclosed translations of the agreed behavior, not independent new scheduling
policy. Existing Phase 01–04 checks remain regression expectations. Reference
comparison is empirical; do not change the confirmed timing just to force a match.

## Implementation Mapping

| File | Responsibility |
|---|---|
| `teamCache/split.h/.c` | Build the inclusive-range block parts, own the FIFO, count completion, and clean queued state. |
| `teamCache/access.h` | Queue linkage and block ordinal in each retained block request. |
| `teamCache/cache_internal.h` | Queue head/tail and the active original-request completion record. |
| `teamCache/access.c` | Start only the FIFO head, retire one READY block per tick, start its successor in the same tick, and notify once on final retirement. |
| `teamCache/CMakeLists.txt` | Compile `split.c`. |
| `tests/teamCache/phase05_split_test.c` | Serial mixed hit/miss flow, same-tick successor start, multi-block metadata, destroy cleanup, and overflow rejection. |

## Commands and Observed Results

Passing checks from the repository root:

```sh
cmake -S . -B /tmp/cadss-phase05-build -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build /tmp/cadss-phase05-build --target teamCache cadss-engine trace processor branch coherence interconnect memory -j 4
gcc -std=c11 -g -O0 -Wall -Wextra -Wpedantic -Werror -Icommon -IteamCache \
  tests/teamCache/phase05_split_test.c teamCache/cache.c teamCache/access.c \
  teamCache/eviction.c teamCache/split.c teamCache/lookup.c teamCache/lifecycle.c \
  -o /tmp/phase05_split_test
/tmp/phase05_split_test
valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all \
  --error-exitcode=99 /tmp/phase05_split_test --skip-overflow
```

- `teamCache` component and required engine framework targets build.
- Phase 05 harness passes. It checks low-block completion followed by same-tick
  high-block activation, later high-block retirement, one processor completion,
  a three-block generalization, dirty fills, destroy cleanup, and child-process
  rejection of `UINT64_MAX,2` before any block work.
- Valgrind reports **26 allocations / 26 frees, 0 bytes at exit, 0 errors**.
  Overflow is separately verified by the child that intentionally exits through
  the confirmed diagnostic path.
- Rebuilt Phase 01 lifecycle, Phase 02 lookup, Phase 03 access, and Phase 04
  eviction harnesses all pass with `split.c` linked. `git diff --check` passes.

Reference observations use `s=0,E=4,b=4` and the valid trace
`L 1f,2; L 1f,2`: verbose lines match reference exactly. teamCache ends at
204 ticks and refCache at 205; this is one occurrence of the documented
inherited Phase 03 hit timing difference. The one-request trace `L 1f,2`
matches at 202 ticks.

## Reference Difference: Split Eviction

The final implementation was rechecked from `master` commit `bbfe7b0` with
`s=0,E=1,b=4` and `L 0,1; L f,2`. `teamCache` takes 305 ticks and reports the
later block `0x10` as `also a Evict`; `refCache` takes 204 ticks and reports it
as `also a Hit`. Both processes exit successfully.

The confirmed teamCache design hits block 0x00, then independently misses block
0x10 and runs the Phase 04 eviction/data path. The reference's result suppresses
that second-block replacement. This conflicts with the roadmap requirement
that both halves independently produce required miss/eviction effects. No
change was made merely to reproduce this binary observation. Phase 05 remains
Partial pending specification/reference clarification.

## Phase 08 Regression Discovery

An ineligible cross-line store trace exposed an implementation bug in verbose
reporting: `trace_access` hard-coded every later split block as `also a Hit`
instead of using that block's actual outcome. It now prints the computed
hit/miss/eviction classification. For `S 0xf,2; L 0x0,1` with 16-byte blocks,
`teamCache` and `refCache` now both report the lower miss, later-block miss,
final hit, and 204 ticks. This output correction does not alter the confirmed
serial split scheduling or the separately documented split-eviction anomaly.
