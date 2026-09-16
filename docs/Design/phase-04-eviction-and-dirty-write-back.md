# Phase 04: Eviction and Dirty Write-Back

## Status

- Design: Confirmed for the single-request LRU path
- Implementation: Implemented
- Acceptance: Accepted locally

The student authorized implementation, testing, and pushing Phase 04. The
later instructor clarification resolved Phase 03 timing: requests start from
a cache tick after arrival. Clean and dirty conflict traces still match at 506
ticks, and mixed hit/eviction `wide.trace` now matches at 2136 ticks.

## Current Implementation Contract

The student requests implementation, testing, and a push of this phase. Their
newly confirmed understanding must also be included in the commit message:
block addresses map to a fixed set; LRU victim selection is restricted to that
set's E lines, never the entire cache. Empty space in another set cannot be used.

The latest decisions supersede historical Open questions below. Translation
details for the confirmed algorithm:

- Add `eviction.h/.c` for selection, starting eviction, and completed eviction.
  The selected target is the first invalid line, otherwise the minimum
  last_access line in the target set. Stable first-way tie handling is a
  deterministic implementation detail; normal access sequence values differ.
- Extend the existing request with a borrowed target-line pointer and the
  old block address. The target is not freed with the request. The original
  operation/address, processor ID, request tag, and callback remain intact.
- Reconstruct the old block address from the selected line's tag and the target
  set index, using unsigned separate shifts by s and b (including s+b=64).
- Miss with invalid target: issue the new-block permReq immediately.
- Full target set: mark WAITING_EVICTION and issue invlReq for the old address.
  Preserve line metadata until completion. If invlReq returns zero, perform
  the eviction-completed transition immediately, as required by the interface.
- Eviction completion: validate processor/old address and state, clear valid,
  then directly issue the original new-block request and enter WAITING_DATA.
- DATA_RECV: validate processor/new address and state, fill the selected target
  with Phase 03 metadata rules, then READY. Tick ordering remains unchanged.
- Current code emits FLUSH_COMPLETE; also recognize the handout's NO_ACTION
  only when it matches an active eviction. Unrelated NO_ACTION does not complete
  a request. Verify the actual event with runtime tracing.
- Both clean and dirty valid victims use invlReq, because coherence owns the
  lower transfer decision and offers no dirty parameter. No extra simulated
  delay or manual writeback count is added.

Testing is explicitly requested. Tests translate the student's selected-set
LRU invariant and confirmed lifecycle: preserve old data while waiting, defer
new fetch until eviction completes, invalidate only on completion, fill only
on new data, keep the original callback, and reclaim request storage. Also
cover clean/dirty victims, immediate interface completion, and regression tests.

## Scope and Non-Goals

Follow `phase-roadmap.md#phase-04-eviction-and-dirty-write-back`:

- Handle a miss when the target set has no invalid line.
- Choose the least-recently-used resident line with deterministic tie handling.
- Preserve victim identity and the original incoming request across eviction.
- Coordinate eviction through coherence `invlReq`, including delayed completion.
- Continue the original miss after eviction requirements have completed.
- Preserve dirty/clean metadata and install replacement only when ready.

No split accesses, RRIP, request queues, write buffer, or victim cache are added.

## Dependencies and Sources

- Phase 01: owned nested line storage, valid/tag/dirty/last_access fields.
- Phase 02: hit-only lookup and established address decoding.
- Phase 03: independent active request, access sequence, dirty update rules,
  coherence-driven fill, and processor completion path.
- `docs/346f26 P1-cache.pdf`, page 5, Section 4.1.
- `common/coherence.h`: actual signature and callback kinds.
- `coherence/coherence.c:invlReq`, `busReq`; `coherence/protocol.c:snoopMI`.
- `interconnect/interconnect.c:tick`: lower completion propagation.
- `teamCache/access.c:cache_access_request`, `cache_access_event`.

## Findings and Constraints

1. Phase 03 currently issues permReq immediately on any miss and looks for a
   free line only when DATA_RECV arrives. A full set then causes a diagnostic.
   Phase 04 must account for eviction before issuing the replacement request:
   the handout explicitly places permReq after required eviction.
2. The real interface is `invlReq(uint64_t addr, int processorNum)` (the PDF
   spells it invReq). Its address is the old victim block, not the new request.
   The return is nonzero when waiting, zero when eviction is already complete;
   this is the opposite waiting convention from permReq.
3. The PDF names NO_ACTION for eviction completion, but current MI code emits
   FLUSH_COMPLETE when an INVALID block receives DATA after eviction. This
   must be verified in a runtime trace before selecting callback handling.
   DATA_RECV and eviction completion must not be conflated.
4. invlReq has no dirty argument. Current MI implementation requests a DATA
   transfer for any coherence state other than INVALID, removes the coherence
   entry, and returns waiting. Even a line marked clean by teamCache may have
   MODIFIED coherence state because this simplified protocol grants that state
   for loads too. Do not assume dirty=0 permits bypassing coherence eviction,
   or that clean eviction has zero modeled delay.
5. Request ownership, hit lookup, access sequence, and refill metadata can be
   reused. Victim information must survive until the eviction path is done;
   it must not be replaced with the incoming block's metadata prematurely.
6. Hit timing remains a separate unresolved reference issue. Phase 04 reference
   acceptance must explicitly account for it rather than hide it with unrelated
   timing changes.

## Student Proposal and Review (Discussion History)

Confirmed ownership direction: after selecting the LRU line in the miss path,
retain that selected line with the current waiting-for-eviction request. This
extends the existing request rather than replacing its incoming operation,
processor identity, request tag, or completion callback. Exact representation
(borrowed line pointer and any retained address/index) remains to be spelled
out; the line's storage is already cache-owned and must not be freed with the
request.

Confirmed invalidation timing: the student chooses to clear the old line's
valid flag after receiving the matching eviction-complete notification, not
when issuing invlReq. The old metadata remains intact during the wait. Combined
with the earlier decision, eviction completion invalidates the old line and
directly issues the original request's new-block fetch; the replacement becomes
valid only when the new data arrives. The interface's immediate-completion case
must use the same logical eviction-completed transition without waiting for a
notification that will not arrive.

The student proposes changing the miss path in memoryRequest: mark the request
not ready, check for an invalid line, and if the set is full actively call
invlReq and move to a waiting-for-eviction state. They ask whether tick needs
special handling for that wait. Victim selection and exact transitions remain
Open. The phrase "if there is a free line, finish" requires clarification:
finishing placement selection still requires issuing permReq for the new block;
returning without issuing it would leave the request waiting forever.

Review clarification: invlReq asks coherence to evict/relinquish the old block;
the current MI implementation performs a transfer for a held block, with no
real data bytes managed by teamCache. Progress occurs through forwarded ticks,
and the eviction-complete event arrives through coherCallback. Waiting alone
does not tell tick that eviction has finished.

Confirmed student direction: the eviction-complete callback directly issues
the original miss's new-block request, rather than deferring issuance to a
separate cache tick. Student asks if the PDF requires this: page 5 says permReq
is called after eviction, but does not specify a callback-vs-next-tick rule for
this internal step. The next-tick processor notification rule is separate.
Source review supports direct issuance: while the old transfer callback is
active, interconnect.busReq enqueues the new BUSWR; after the callback returns,
the old pending transfer is freed. Later lower ticks service the queued request.
This is source review, not yet runtime-verified eviction behavior.

## Resolved Design and Remaining Follow-Ups

- Confirmed: choose minimum last_access only within the incoming block's set.
  Use available invalid storage in that set before considering an eviction.
- Confirmed: keep the victim associated with the existing request, preserving
  the incoming address, original processor/request identifier, and callback.
- Confirmed: keep old line metadata valid until eviction completion; only then
  invalidate it and directly issue the original new-block fetch.
- Confirmed: three states, WAITING_EVICTION, WAITING_DATA, READY; idle remains a
  null active-request pointer. Processor notification timing is unchanged.
- Implemented mechanics: borrowed target pointer, retained victim block address,
  stable first-way tie handling, immediate invlReq completion, and guarded
  matching of the actual FLUSH_COMPLETE / documented NO_ACTION callback.
- Remaining: professor clarification of Phase 03 hit timing. No new eviction
  policy or ordering decision remains unresolved for the implemented path.

## Confirmed Pseudocode

Confirmed behavior to date:

```text
on a miss in a full set:
    select the valid line with minimum last_access
    associate that victim with the existing incoming request
    issue eviction for the victim's block; retain its metadata while waiting
on matching eviction completion:
    clear the victim's valid flag
    directly issue the original request's new-block fetch
    wait for incoming data rather than notify the processor
on incoming data:
    fill cache metadata using the Phase 03 rules and mark the request READY
```

Concrete fields and callback matching follow the current contract above;
implementation and tests below verify these transitions.

## Acceptance Behaviors

Inherited from the roadmap:

- Prefer invalid storage over evicting a valid line.
- Full sets evict the LRU line deterministically.
- Clean and dirty victims follow the required coherence path.
- Do not overwrite the victim while its eviction is outstanding.
- Preserve original address, processor, request ID, and callback across waits.
- Install the replacement only after required data/permission is ready.
- Conflict traces agree with reference classification and timing, with the
  existing Phase 03 timing discrepancy tracked explicitly until resolved.

## Implementation Mapping

| File | Change |
|---|---|
| `teamCache/eviction.h/.c` | Target-set placement/LRU, old address reconstruction, eviction start/completion |
| `teamCache/access.h` | WAITING_EVICTION, borrowed target pointer, victim address |
| `teamCache/access.c` | Fetch after eviction; matching completion events; refill selected target; verbose eviction classification |
| `teamCache/cache.c` | Pass borrowed coherence to the event handler |
| `teamCache/CMakeLists.txt` | Build eviction.c |
| `teamCache/tests/phase03_access_test.c` | Adapt to internal event interface; prior expectations unchanged |
| `teamCache/tests/phase04_eviction_test.c` | Victim selection, event ordering, metadata, ownership checks |
| `teamCache/tests/phase04_*.trace` and `.config` | Reproducible reference traces |

The access sequence and hit completion timing are unchanged. Requests own no
line memory, and destroy frees pending request storage before line storage.

## Commands and Observed Results

### Passing checks

- CMake teamCache target builds.
- Strict C11 compilation with `-Wall -Wextra -Wpedantic -Werror` passes for
  all four phase harnesses and their required component sources.
- Phase 04 harness passes: target-set LRU despite an older line and free space
  elsewhere, invalid-way preference, dirty victim preserved during wait, no
  early new fetch, direct fetch from eviction callback, metadata replaced only
  on new data, exactly-once original completion, clean victim, high 64-bit old
  address with nonzero set index, handout NO_ACTION compatibility, immediate
  invlReq completion, and public destroy during pending eviction.
- Valgrind Phase 04 harness: **46 allocations / 46 frees, 0 bytes at exit,
  0 errors**.
- Rebuilt Phase 01 harness passes all 10 allocation-failure checkpoints.
- Rebuilt Phase 02 harness passes all 18 cases.
- Rebuilt Phase 03 harness passes its existing timing/dirty/ordering/lifetime
  expectations with the adapted internal function signature.

Run from the repository root:

```sh
cmake --build /tmp/cadss-phase01-build --target teamCache -j 4
gcc -std=c11 -g -O0 -Wall -Wextra -Wpedantic -Werror -Icommon -IteamCache teamCache/tests/phase04_eviction_test.c teamCache/cache.c teamCache/access.c teamCache/eviction.c teamCache/lookup.c teamCache/lifecycle.c -o /tmp/cadss-phase01-build/phase04_eviction_test
/tmp/cadss-phase01-build/phase04_eviction_test
valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=99 /tmp/cadss-phase01-build/phase04_eviction_test
```

For Phase 03 replace the harness name with phase03_access_test. For Phase 01 use
phase01_lifecycle_test and add `-Wl,--wrap=calloc -Wl,--wrap=free`. Phase 02 retains
its documented lookup/lifecycle-only build command. Phase 01/03 builds now need
`eviction.c` because access.c uses the new module.

### Actual framework callback verification

A read-only GDB run of the dirty conflict trace observed:

```text
DATA_RECV      address=0x0
FLUSH_COMPLETE address=0x0
DATA_RECV      address=0x20
FLUSH_COMPLETE address=0x20
DATA_RECV      address=0x0
```

Numeric event values were 1/3/1/3/1, matching common/coherence.h. The process
exited normally at 506 ticks. Script `/tmp/phase04-events.gdb` breaks on
cache_access_event and prints type/address; approved escalated GDB was used
because the sandbox does not permit ptrace. This verifies the actual callback
spelling and the callback-driven fetch path in the supplied hierarchy.

### Reference comparison

All runs exit 0 with empty stderr. Exact verbose address/classification lines
match, including `Evict`, `Dirty Evict`, and the reference's `(nil)` spelling
for address zero.

| Trace | Config | teamCache ticks | refCache ticks | Result |
|---|---|---:|---:|---|
| phase04_clean_conflict | phase04_conflict | 506 | 506 | Exact match |
| phase04_dirty_conflict | phase04_conflict | 506 | 506 | Exact match |
| phase04_set_lru | phase04_set_lru | 711 | 714 | Classification matches; three inherited hit ticks |
| phase03_one_hit | phase03_one_hit | 103 | 104 | Existing Phase 03 discrepancy unchanged |

All fixtures are checked in under teamCache/tests. Conflict config is s=1/E=1/
b=4; set-LRU config is s=2/E=2/b=4; all explicitly set i=u=w=0. The set-LRU
trace keeps an older block at 0x0 in another set and leaves still other sets
empty while forcing replacement among 0x10/0x50/0x90. Later hits confirm the
other-set block survived and the correct target-set victim was chosen.

Run from `/tmp/cadss-phase01-build`:

```sh
/home/chenyy/cadss_public/cadss-engine -c teamCache -s /home/chenyy/cadss_public/teamCache/tests/phase04_conflict.config -t /home/chenyy/cadss_public/teamCache/tests/phase04_clean_conflict.trace -v
/home/chenyy/cadss_public/cadss-engine -c /home/chenyy/cadss_public/refCache -s /home/chenyy/cadss_public/teamCache/tests/phase04_conflict.config -t /home/chenyy/cadss_public/teamCache/tests/phase04_clean_conflict.trace -v
```

Repeat for dirty_conflict, and use phase04_set_lru.config with its corresponding
trace. Logs are `/tmp/cadss-phase01-build/phase04_<case>-<component>.log`.

## Limitations and Follow-Ups

- Phase 04 is implemented and its eviction behaviors pass locally, but full
  reference acceptance is not claimed while inherited hit timing differs.
- No framework/coherence source edits, manual tick penalties, RRIP, split
  accesses, queues, or write buffering were added.
- The previously documented full-build zlib limitation remains; tested builds
  use the available framework and component targets.
- A future expansion of coherence behavior must preserve event matching and
  review the simplified MI assumptions; a permReq granting permission on a
  lookup miss still uses the existing Phase 03 diagnostic.
