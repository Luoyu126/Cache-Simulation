# Phase 03: LRU Hits, Misses, and Fills

## Status

- Design: Hit timing interpretation under review; no revised state machine confirmed
- Implementation: Implemented for the confirmed normal path
- Acceptance: Partial; reference hit timing differs

The student requests committing/pushing the current implementation while asking
Professor Railing about the timing interpretation. The English Ed draft is saved
in `docs/ed-cache-hit-timing-question.md`; it has not been posted or sent by the
agent. Current code retains next-cache-tick hit completion pending clarification.

Phase 04 integration note: the internal event handler now also receives the
borrowed coherence pointer, allowing a completed eviction to issue a fetch.
Its existing tests use that signature. Builds linking access.c must also link
eviction.c; historical Phase 03 commands below describe the earlier snapshot.
Hit timing and dirty/access-sequence expectations are unchanged.

## Implementation Readiness Review

### Latest student correction: reference hit timing is the target

Further clarification: the student explains the 104 expectation as a baseline
ending at 102, hit arriving at 103, and callback at 104. Therefore the earlier
statement below must not be taken as approval of two tick calls after receipt
or of an extra HIT_PENDING state. The issue is the receipt round. Processor
clears pendingMem in the completion callback and calls getNextOp later within
that same outer iteration. In round 102 it either observes EOF (baseline) or
issues the next load (two-load trace). GDB already confirmed issuance at 102.
No implementation timing change is authorized by this clarification.

After the single-hit experiment, the student explicitly decides a hit must add
two ticks. This supersedes next-cache-tick notification for hits in the contract
below. Current code still adds one tick and must be revised. Miss timing already
matches the tested reference and is not to gain another tick.

Agent proposal for review: on hit retain a distinct hit-processing state; at the
next cache tick transition it to READY after existing READY completions; notify
at the following cache tick. This realizes T issue, T+1 READY, T+2 callback.
The student has confirmed the latency target, not this new state representation.
No timing code was changed in this exchange.

### Current implementation contract (supersedes historical Open items below)

Confirmed by the student's latest message: use one cache-owned access sequence;
update only the accessed line on a hit or completed fill. Sequence increments
once per such event, never during waiting. It is not simulated time.

All normal-path behavior is now specified by the preceding student decisions:

```text
memoryRequest(op, processor, request_tag, callback):
    preserve an independent copy and the original completion information
    lookup the requested address
    on hit: update last_access; set dirty for a store, preserve it for a load
            mark the request READY
    on miss: mark WAITING_DATA and issue one coherence request
DATA_RECV for the waiting request:
    find an invalid line in the addressed set
    fill tag/valid; dirty = whether the original operation is a store
    assign the next access sequence to last_access; mark request READY
tick:
    notify and release the request already READY on entry, if any
    tick coherence (any newly READY request waits until next tick)
destroy:
    free any remaining request copy, then cache storage and public interface
```

Implementation mechanics translating this contract:

- `access.h/.c` implements request acceptance, coherence events, tick, and
  pending-request cleanup. `cache.c` keeps the state and delegates to them.
- The request allocation embeds the value-only trace_op copy with processor ID,
  original request tag, callback pointer, and the two-state enum. One nullable
  active pointer represents the Phase 03 request; no map/queue is added.
- The existing cache_state owns that pointer and the access sequence. Destroy
  frees the request before storage reset. The callback receives the original
  processor/tag; these are not address tags.
- Lower-layer addresses use block starts (clear the low b bits), preserving
  the same-block identity of the existing lookup. DATA_RECV is matched against
  the pending block and processor. No payload bytes are copied.
- Pick the first invalid way during fill; no victim selection is introduced.
- Outside-phase requests (full-set fills, split accesses, overlapping requests,
  RRIP/write-buffer accesses) and malformed events fail with a diagnostic;
  they must not silently complete, overwrite state, or wait forever. Empty
  initialization with future options remains supported as in Phase 01.
- A nonzero permReq on a lookup miss is diagnosed as an unexpected consistency
  result in this normal-path implementation, as discussed with the student.
  NO_ACTION does not complete a fill; invalidation/writeback handling remains
  deferred. Allocation failure and sequence overflow fail explicitly.

Latest metadata decisions: store hit sets dirty=1 (Confirmed); a load-miss
fill sets dirty=0 (Confirmed); a store-miss fill followed by completion of the
original store sets dirty=1 (Confirmed). Read hits preserve the existing dirty
value. The student explicitly accepts the store-miss rule after discussing
an alternative that synchronizes memory on write misses only; that alternative
is not selected for this framework. All dirty update rules are now resolved.
The student requested a recommendation for last_access and has now confirmed it: maintain a cache-owned monotonically increasing access
sequence and assign the next value when updating a hit or installing a fill.
This records LRU order without coupling it to modeled latency. Later concurrent
or queued behavior must revisit ordering within its own phase.

The student now requests implementation of the normal flow. Confirmed scope:
copy each incoming request into owned storage; hit updates the line and marks
READY; miss issues coherence once and waits; DATA_RECV selects an invalid line,
fills it, and marks READY; tick completes existing READY work before advancing
coherence; completed copies are freed and destroy cleans remaining copies.

Pre-implementation questions sent to the student: choose the source and update
events for last_access, and specify dirty after a store hit, load fill, and
store fill. Preserving dirty on a read hit is already confirmed. These were
explicitly deferred in prior records and cannot silently be treated as resolved
by the request to implement. Those questions are now resolved by the current contract above.

The recent permReq discussion clarified that immediate permission success is
not an expected ordinary cold-miss scenario in the consistent current MI path;
it is not evidence of a victim cache. The interface still defines a nonzero
result, so it must not silently create an endless wait if encountered.

Readiness checks: reran the existing binaries
`/tmp/cadss-phase01-build/phase01_lifecycle_test` and
`/tmp/phase02_lookup_test`; both passed (10 allocation-failure checkpoints and
18 lookup cases respectively). These are existing baseline checks, not a
rebuild or Phase 03 validation. Re-read README, phase dependencies, current
interfaces, and handout Section 4.1. No goals.md was found in the file search.

## Scope and Non-Goals

Follow `phase-roadmap.md#phase-03-lru-hits-misses-and-fills`:

- Connect hit lookup to the processor request path.
- Retain request information after `memoryRequest()` returns.
- Process one foreground request at a time, contained within one block.
- Implement LRU metadata updates and store-related dirty-state behavior.
- On a miss with available space, find an invalid line in the placement path,
  interact with coherence, and install the block when data/permission is ready.
- Notify the requesting processor exactly once at the required later tick.

Non-goals: evicting valid lines (Phase 04), split accesses (Phase 05), RRIP
behavior (Phase 06), request queueing (Phase 07), and write buffering (Phase 08).
This phase is not the complete assignment implementation.

## Dependencies and Relevant Sources

- `phase-01-initialization-and-lifecycle.md`: allocated storage, persistent
  private state, zeroed valid/tag/timestamp/dirty fields, borrowed coherence.
- `phase-02-address-decoding-and-lookup.md`: lookup returns a borrowed matching
  line pointer or NULL and does not mutate state or select placement targets.
- `docs/346f26 P1-cache.pdf`, Sections 3, 4, 4.1: timing, request identity,
  and coherence interface requirements.
- `common/cache.h`, `common/trace.h`, `common/coherence.h`: actual C interfaces.
- `processor/processor.c:tick`, `memOpCallback`: caller lifetime and timing.
- `teamCache/cache.c:memoryRequest`, `tick`, `coherCallback`: starter behavior
  that must be replaced as part of this phase's confirmed design.
- `coherence/coherence.c:permReq`, `busReq`, `tick` and
  `coherence/protocol.c:cacheMI`, `snoopMI`: lower-layer progress and callbacks.
- `interconnect/interconnect.c:tick`: memory and bus progress during a tick.
- `refCache/librefCache.so`: later differential validation reference.

## Research Findings: Framework Constraints

These describe the existing specification/code, not a proposed cache algorithm.

1. Processor sets its request pending flag, calls cache `memoryRequest()`, then
   frees `nextOp` in the same call to processor `tick()`. Keeping only a
   `trace_op*` would leave a dangling pointer. Relevant request data must remain
   available across the lower-memory delay.
2. The `tag` argument of `memoryRequest()` is the processor's request identifier;
   it is not the cache-line tag derived from the memory address. The actual
   callback type in the headers is `void (*)(int, int64_t)`.
3. Processor calls cache `tick()` before looking for new trace requests during
   that processor tick. Cache currently ticks coherence, which ticks the
   interconnect, which ticks memory. Coherence may call cache back inside this
   nested call stack. All of this is synchronous C execution within a simulated
   tick, not separate OS threads.
4. Handout specifies processor notification on the next tick after a hit or
   completion. A return from `memoryRequest()` is not request completion.
   Exact event ordering must be reviewed in the student's design, especially
   when lower-level completion arrives inside cache `tick()`.
5. Handout's simplified miss interface is `permReq(false, addr, procNum)`:
   nonzero return means permission/data is available to proceed; zero means
   wait. The supplied MI code sets this result explicitly. `DATA_RECV` is the
   data-request completion event; do not treat every callback kind as a fill.
6. Coherence keys its state by the supplied address (`getState`/`setState`), so
   the representation of an addressed block and callback matching must be
   consistent. Block-address handling was deliberately deferred from Phase 02
   to its consumers and now needs review in the request/fill path.
7. Existing starter code always issues `permReq`, ignores its return, conflates
   `NO_ACTION` and `DATA_RECV`, and does not clear completion after notifying.
   It also completes a previous pending request when another arrives. These
   are starter placeholders, not approved timing/queueing design to preserve.

## Inherited Student Decisions

- `cache.c` owns the internal state and delegates to separate C/header modules.
- `cache_lookup()` remains hit-only and read-only.
- Placement groups finding available storage with later full-set victim
  selection; only the invalid-line part belongs to this phase.
- The line's last-access timestamp and dirty marker already exist and start
  at zero. Their update rules are now confirmed in the current contract above.

## Student Proposal and Reasoning (Historical Discussion)

- Latest student summary: memoryRequest retains the request and marks hits
  READY or misses WAITING; the data callback selects an invalid line and fills
  it; tick completes already-READY requests before lower-level progress.
  Invalid-line selection on data-ready handling is now the student's proposal.
  Review reminders: hit metadata updates and actual coherence issuance are
  required; the data callback marks READY after fill during the lower tick.
  Immediate-success permReq handling remains Open (must not wait for a callback
  that will not arrive). Phase 03 still processes one foreground request;
  processing "all" ready requests does not confirm a queueing scope change.

- Proposed by the student: first lookup; on hit update last-access time and
  dirty metadata; on miss wait for memory data and fill the cache.
- Review clarification: lower-level requests go through coherence, which
  drives interconnect/memory. Processor completion notification is also needed
  at the specified tick boundary. Detailed request state and ordering are Open.
- Confirmed by the student: reading a line whose dirty bit is 1 leaves it 1.
  This resolves the write-then-read concern; a read does not undo outstanding
  modifications. Full store and fill update rules remain to be specified.
- Confirmed by the student: copy incoming `op` into cache-owned storage before
  returning and use the independent copy thereafter. The original proposal
  reclaimed storage in `destroy()`; the lifetime is refined below.
- API review: `trace_op` contains only value fields, an inline array, and a
  value union, with no pointer members. Struct assignment into independent
  storage copies all its contents; recursive deep-copy logic is unnecessary.
- Confirmed lifetime refinement: release each request's independent copy when
  that request completes. `destroy()` cleans up any remaining unfinished copy;
  completed copies are not retained until shutdown or freed a second time.
  This supersedes the destroy-only option; shared reusable storage was not
  selected. Processor
  identity, request identifier, and callback arrive separately from `op` and
  their retention still needs to be specified.
- Proposed by the student: associate distinct request identifiers with separate
  request copies so overlapping requests do not overwrite each other's data.
  Review: this is a valid identity/storage association; the framework already
  supplies a request tag. It does not itself define scheduling or permit
  concurrent lower-level requests. Container choice remains Open; completed-copy
  cleanup is now Confirmed above. Multi-request queueing remains Phase 07; no roadmap expansion
  has been confirmed.

### Timing Responsibility Clarification

Superseded state proposal: a request status field with values 0/1/2 for
waiting on the lower hierarchy, data received this tick, and data received on
the previous tick. The student understands the need to retain progress between
entry-point calls. Review questions remain: how absence of a request is
represented, which status a hit receives, and exactly where status promotion
occurs relative to the lower tick and completion check. In particular a data
callback nested inside the lower tick must not cause same-tick notification.
Confirmed representation refinement: the student chooses descriptive `enum`
constants instead of raw 0/1/2 values. This supersedes numeric spelling in the
proposal, not the still-open state semantics. Exact names, the final set of
states, and the transition algorithm remain unconfirmed.

Superseded student transition proposal: a hit enters the received/ready
this-tick state. Each cache tick first notifies requests marked ready on the
previous tick, then promotes this-tick-ready requests to previous-tick-ready.
Review counterexample: a hit received after cache tick T enters this-tick-ready;
tick T+1 only promotes it, so notification occurs at T+2 rather than T+1.
The placement of the lower-level tick in this sequence is also unspecified.
Student revision is needed before implementation; no corrected ordering has
been selected on the student's behalf. Traversal of multiple requests remains
a proposal for later queueing, not an approved Phase 03 scope expansion.

Confirmed replacement: two named states, `REQUEST_WAITING_DATA` and
`REQUEST_READY`, suffice for the active Phase 03 request. The student explicitly
confirms that each cache tick first completes a request already READY on entry,
then advances coherence. A request becoming READY during that lower tick is
left for the next cache tick. Hit handling marks the request READY; data-ready
handling marks it READY after fill. No this-tick/previous-tick promotion is
needed. Absence-of-request representation remains Open.

Confirmed responsibility split: the student adopts `memoryRequest` for copying
the request, lookup, hit handling or miss issuance; `coherCallback` for handling
data-ready notification and fill; and `tick` for advancing lower components and
notifying the processor at the required boundary. These entry points cooperate
through persistent request state, not a blocking all-in-one request function.
The two-state tick ordering is now Confirmed above; other request details remain
Open. Phase modules
may provide helpers behind these entry points, preserving the earlier concise
`cache.c` requirement.

Framework clarification: ticks are synchronous calls, not real-time deadlines.
Ordinary lookup/metadata code finishes before the framework can advance to its
next tick; simulated waiting instead persists across returned calls.

Latest student timing proposal (Proposed): a hit is ready for notification on
the next tick; a miss issues a coherence request, keeps forwarding framework
ticks, and notifies the processor on the tick after data arrives. The student
asks whether miss issuance itself should wait until the next tick. Review:
the next-tick notification requirement does not itself require delaying request
issuance; `permReq` can be called within `memoryRequest`. Delaying issuance may
add a modeled cycle. Issuance timing remains to be confirmed. Notification means
calling the processor callback, not returning from the original C call. Also
retain the immediate-success `permReq` case; not every miss must wait for a
later data callback.

The student asks whether cache reports numeric hit/miss costs or the simulator
counts cycles, and whether processor stalls must be implemented in cache.

Source-grounded clarification (not a confirmed request-state design):

- Engine repeatedly calls processor `tick()`. Processor increments `tickCount`
  once per tick and prints it in `finish()`; cache does not return a latency
  integer. The return value of cache `tick()` is not a number of elapsed cycles.
- Cache determines completion timing through the processor callback, subject
  to the handout's next-tick requirement. Misses depend on progress and events
  from coherence/interconnect/memory, rather than a cache-chosen fixed penalty.
- Processor already blocks on `pendingMem` and clears it in `memOpCallback`.
  Cache must obey timing and completion rules; it need not implement the
  processor pipeline or its stall mechanism.
- Later write-buffer work can affect overlap and thus measured ticks. Phase 03
  remains focused on correct single-request behavior and timing. Host program
  execution speed is distinct from the simulated tick count.

The student clarified that the concern was one versus two lookup/placement
scans, and acknowledged the distinction: extra C work within the same call
does not itself advance simulated time. The existing separate hit lookup and
placement boundary remains unchanged. The student next requests a functional
scope summary; no implementation or request-state design is confirmed yet.

## Open Design Questions

### Framework Entry-Point Clarification

The student asks whether `cache.c` needs a main/request-receiving loop.
Source review confirms that `engine/engine.c:main` owns the program loop and
calls processor ticks. `teamCache` is a shared-library component, not a separate
process. Its `init` registers function pointers for `memoryRequest`, `tick`,
`finish`, and `destroy`, and registers `coherCallback` with coherence. The cache
implements these interfaces and retains state between calls; it does not add
a blocking loop to wait for memory, since subsequent framework ticks drive
lower-level progress. This clarifies framework mechanics, not a student-chosen
request state machine.

### Remaining Decisions

The normal-path state, metadata updates, and implementation mapping are resolved
by the current contract. The outstanding acceptance decision is the reference
hit timing discrepancy below. Multi-request scheduling, eviction, RRIP,
split accesses, and write buffering remain in their roadmap phases.

## Confirmed Pseudocode

The full normal-path pseudocode is in the current contract above. The key
student-confirmed single-request tick ordering is:

```text
cache tick:
    if an active request is already READY:
        notify its processor of completion
        release the completed request's copy and remove it from active tracking
    advance coherence by one tick
    leave any request made READY during that call for the next cache tick
```

Hit handling makes the request READY after its line updates. Data-ready handling
makes it READY after fill. Implementation follows the current contract above.

## Acceptance Behaviors and Test Scenarios

Inherited roadmap requirements:

- First access to an absent block misses and obtains data through coherence.
- Later accesses, including a different byte in that block, reuse the resident
  line without another DRAM fetch.
- Store hits update the required dirty state.
- No synchronous processor callback from the original request call.
- Exactly one callback per accepted request with its original processor number
  and request identifier.
- Supported no-eviction traces agree with `refCache` in verbose classification
  and total ticks.

The tests translate student-confirmed behaviors: next-tick hit/fill completion,
read-after-write preservation, dirty load/store fills, per-access ordering, and
request ownership. Agent-authored concrete traces are disclosed below; they do
not add a new replacement algorithm. Reference results are observations, not
student approval to change the timing design.

## Implementation Mapping

| File | Responsibility |
|---|---|
| `teamCache/access.h` | Request copy, completion identity, two-state enum, internal APIs |
| `teamCache/access.c` | Hit/miss, fill, metadata, completion-before-lower-tick, request cleanup |
| `teamCache/cache_internal.h` | Owned active-request pointer and access sequence |
| `teamCache/cache.c` | Short framework wrappers and lifecycle integration |
| `teamCache/CMakeLists.txt` | Compile the access module |
| `teamCache/tests/phase03_access_test.c` | Mock lower-tick delivery and contract checks |

## Commands and Observed Results

### Passing checks

- `cmake --build /tmp/cadss-phase01-build --target teamCache -j 4`: passes.
- Strict C11 build of the Phase 03 harness and all component sources: passes.
- Phase 03 harness: passes. Covers idle tick forwarding, freed input op,
  non-synchronous completion, nested DATA_RECV, no duplicate requests/callbacks,
  dirty semantics, access sequence, same-block hits, high-bit address/tag
  preservation, and public destroy with WAITING/READY requests.
- Valgrind on that harness: 35 allocations / 35 frees, zero bytes at exit,
  zero errors. Component lifecycle owns all request copies.
- Rebuilt Phase 01 harness: passes all 10 allocation-failure checkpoints.
- Rebuilt Phase 02 harness: all 18 cases pass.
- `git diff --check`: passes.

Reproduction commands from the repository root:

```sh
cmake --build /tmp/cadss-phase01-build --target teamCache -j 4
gcc -std=c11 -g -O0 -Wall -Wextra -Wpedantic -Werror -Icommon -IteamCache teamCache/tests/phase03_access_test.c teamCache/cache.c teamCache/access.c teamCache/lookup.c teamCache/lifecycle.c -o /tmp/cadss-phase01-build/phase03_access_test
/tmp/cadss-phase01-build/phase03_access_test
valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=99 /tmp/cadss-phase01-build/phase03_access_test
gcc -std=c11 -g -O0 -Wall -Wextra -Wpedantic -Werror -Icommon -IteamCache teamCache/tests/phase01_lifecycle_test.c teamCache/cache.c teamCache/access.c teamCache/lookup.c teamCache/lifecycle.c -Wl,--wrap=calloc -Wl,--wrap=free -o /tmp/cadss-phase01-build/phase01_lifecycle_test
/tmp/cadss-phase01-build/phase01_lifecycle_test
gcc -std=c11 -g -O0 -Wall -Wextra -Wpedantic -Werror -Icommon -IteamCache teamCache/tests/phase02_lookup_test.c teamCache/lookup.c teamCache/lifecycle.c -o /tmp/cadss-phase01-build/phase02_lookup_test
/tmp/cadss-phase01-build/phase02_lookup_test
```

### Reference comparison: timing discrepancy remains

Configuration `/tmp/cadss-phase01-build/phase03.config` contains:

```text
__processor
__cache -s 1 -E 2 -b 4
__branch
__coherence
__interconnect
__memory
```

Each semicolon below is a newline in the plain-text trace; no leading spaces.
Files are `/tmp/cadss-phase01-build/phase03-<name>.trace`.

| Name | Trace | teamCache ticks | refCache ticks |
|---|---|---:|---:|
| load | L 10,1 | 102 | 102 |
| hits | L 10,1; L 18,1; S 10,1; L 10,1 | 105 | 108 |
| store | S 20,1; L 28,1 | 103 | 104 |
| two | L 10,1; L 20,1; L 10,1 | 204 | 205 |
| cold_pair | L 10,1; L 20,1 | 203 | 203 |

All five runs exit 0, have empty stderr, and match verbose block-address hit/miss
classification exactly. In these traces, reference timing is one additional
tick per hit. Pure cold misses match. The implementation follows the student's
explicit next-cache-tick hit completion; no extra delay was inserted to match
the binary. This is a specification/reference discrepancy requiring student
review, not grounds to silently alter the confirmed ordering.

Run from `/tmp/cadss-phase01-build` (repeat with each name):

```sh
/home/chenyy/cadss_public/cadss-engine -c teamCache -s phase03.config -t phase03-hits.trace -v
/home/chenyy/cadss_public/cadss-engine -c /home/chenyy/cadss_public/refCache -s phase03.config -t phase03-hits.trace -v
```

Combined output logs are `phase03-<name>-teamCache.log` and
`phase03-<name>-refCache.log` in that build directory. The engine writes a NUL
after its Ticks line. Early exploratory runs incorrectly used leading spaces
(the reader then stops immediately) and a relative reference path unavailable
from the build directory; those outputs were rejected and replaced with the
corrected runs above.

## Deviations and Follow-Ups

### Student-requested single-hit testcase

The student requests testing exactly one hit. Cold initialization requires a
warm-up miss, so the checked-in `teamCache/tests/phase03_one_hit.trace` contains
two identical loads (`L 10,1` twice); `phase03_cold_load.trace` contains only the
first load as baseline. `phase03_one_hit.config` explicitly disables victim
cache, subblocking, and write buffering. Expected classifications are Miss/Hit
and Miss respectively; all four runs passed these checks, exited 0, and had
empty stderr.

| Component | Cold-load baseline | Baseline plus one hit | Difference |
|---|---:|---:|---:|
| teamCache | 102 | 103 | 1 |
| refCache | 102 | 104 | 2 |

Reproduce from `/tmp/cadss-phase01-build`:

```sh
/home/chenyy/cadss_public/cadss-engine -c teamCache -s /home/chenyy/cadss_public/teamCache/tests/phase03_one_hit.config -t /home/chenyy/cadss_public/teamCache/tests/phase03_one_hit.trace -v
/home/chenyy/cadss_public/cadss-engine -c /home/chenyy/cadss_public/refCache -s /home/chenyy/cadss_public/teamCache/tests/phase03_one_hit.config -t /home/chenyy/cadss_public/teamCache/tests/phase03_one_hit.trace -v
```

For the baseline, replace the trace filename with `phase03_cold_load.trace`.
Logs are `phase03_<cold_load|one_hit>-<teamCache|refCache>.log` in that build
directory. No timing implementation was changed by this experiment.

### Processor call-order investigation

Direct receipt verification requested by the student: GDB breakpoint at
`teamCache/cache.c:57` (memoryRequest entry), not merely the processor call site,
using the checked-in two-load testcase. Observed:

```text
RECEIPT round=1   tickCount=1   address=0x10 request_tag=0
COMPLETE round=102 tickCount=101 request_tag=0
RECEIPT round=102 tickCount=102 address=0x10 request_tag=256
COMPLETE round=103 tickCount=102 request_tag=256
```

The second receipt backtrace is cache.c:memoryRequest <- processor.c:160 tick
<- engine.c:455 main. Thus cache actually receives the second request in outer
round 102, after the prior miss completes, not merely when the trace is read.
A pending miss does not allow the next request; completing it clears pendingMem
and permits same-round issuance. Script `/tmp/phase03-receipt.gdb` ran via
approved escalated GDB with `-c teamCache`, the checked-in
`phase03_one_hit.config` and `phase03_one_hit.trace`, and `-v`. Process exited
normally with 103 ticks. No implementation code changed.

Optional-feature hypothesis: the student asks whether victim buffering or other
features account for the extra hit tick. Ran reference with explicit
`-i 0 -u 0 -w 0`, then with `-i 4 -u 0 -w 0`, retaining s=1/E=2/b=4.
Both configurations yielded 102 ticks for `phase03-load.trace` and 108 for
`phase03-hits.trace`, with identical classifications, exit 0, and no stderr.
These traces have no evictions, so no victim entry is populated. Results rule
out enabling victim cache as the explanation in this example, but do not
identify the binary's internal scheduling or rule out unconditional overhead.
Configs/logs are `phase03-explicit_off.config`, `phase03-victim_on.config`, and
`phase03-<configuration>-<load|hits>-refCache.log` in the existing build directory.

PDF follow-up: re-read all seven pages with pypdf. Page 2, Section 3 says a hit
or completed request should notify the processor via callback on the next tick.
It does not explicitly impose one extra lookup tick or say notification occurs
two cache-tick calls after memoryRequest. Page 5, Section 4.1 says to call the
coherence tick ("first?"); it does not resolve hit notification timing. Thus the
PDF alone does not explain the observed round-102 issue / round-104 completion.
An additional hit-processing stage or a different tick-boundary interpretation
would be possible explanations, not established facts about the binary. Page 7
assigns credit for matching reference output and explicitly acknowledges possible
lab bugs and recommends a minimal reproduction. No timing change was made.

Student hypothesis: processor might issue memoryRequest and then tick cache in
the same outer iteration, accidentally completing a hit immediately.
Source and GDB runtime checks reject that hypothesis for this processor:
`processor.c:112` calls cache tick, line 113 increments tickCount, and line 160
issues a new request. Engine calls processor tick once per main-loop iteration.

GDB breakpoints counted processor-tick entries separately from tickCount. For
`phase03-hits.trace`, observed teamCache ordering:

```text
outer round 102: cache tick, old miss completion (tickCount=101)
outer round 102: issue first hit (tickCount=102)
outer round 103: cache tick, first hit completion (tickCount=102)
outer round 103: issue second hit (tickCount=103)
outer round 104: cache tick, second hit completion (tickCount=103)
outer round 104: issue third hit (tickCount=104)
outer round 105: cache tick, third hit completion (tickCount=104)
```

Thus matching numeric tickCount at issue/completion does not mean the same
outer iteration: increment occurs after cache tick. Reference runtime tracking
confirmed first hit issued in round 102 completes in round 104; subsequent hits
are issued in rounds 104/106 and complete in rounds 106/108. The extra iteration
is real and not inferred solely from total ticks. This does not establish the
reference's internal rationale or authorize changing the student design.

Commands: GDB batch scripts `/tmp/phase03-order.gdb` (teamCache) and
`/tmp/phase03-reference-order.gdb` (reference) ran the existing engine with
`-s /tmp/cadss-phase01-build/phase03.config -t
/tmp/cadss-phase01-build/phase03-hits.trace` from that build directory. Both
debugged processes exited normally. Sandbox ptrace was unavailable; approved
escalated read-only GDB runs succeeded. No implementation code was changed.

- **Not Accepted:** resolve reference hit timing with the student before changing
  request timing or claiming Phase 03 reference acceptance.
- Full-project build remains subject to the previously documented missing
  `zlib.h`; this phase built the component and used available framework targets.
- No full-set replacement, split handling, queueing, RRIP, or write buffering
  was added. Such accesses terminate with a clear phase diagnostic.
- Unexpected nonzero permission and unsupported coherence events are diagnosed;
  broader recovery is not part of this single-request normal-path phase.
