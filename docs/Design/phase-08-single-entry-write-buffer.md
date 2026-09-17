# Phase 08: Single-Entry Write Buffer

## Status

- Design: Confirmed for delayed-callback independent-hit behavior
- Implementation: Implemented
- Acceptance: Accepted locally

## Scope and Non-Goals

Phase 08 implements required `-w 1` behavior:

- Preserve all existing behavior when `-w 0` is selected.
- Allow one eligible single-line store miss to occupy an independent write-buffer
  slot and progress through the lower hierarchy in the background.
- Allow only independent foreground cache hits while that slot is occupied.
- Hold a second miss or another request requiring the occupied buffer until
  the buffered miss completes.
- Keep foreground and background ownership, callback identity, coherence
  events, targets, and progress separate.

Write-buffer modes `-w 2` and above, victim cache, and subblocking remain out
of scope.

## Relevant Specification and Code

- `docs/346f26 P1-cache.pdf`, page 3: `-w 1` buffers one write miss;
  independent read/write hits may execute until a second access misses;
  unaligned accesses do not buffer.
- `docs/Design/phase-roadmap.md`: Phase 08 scope and acceptance behavior.
- `teamCache/lifecycle.c`: already accepts `-w 0` and `-w 1`.
- `teamCache/access.c`: routes foreground/background misses and coherence
  events, performs early store acknowledgment, and resumes queued work.
- `teamCache/request_queue.c`: retains arriving original requests in FIFO
  order until a cache tick starts one.
- `teamCache/split.c`: serializes the blocks of the current original request.
- `teamCache/eviction.c`: accepts an explicit foreground or buffered request.
- `traces/cache/wb-test.trace`: one initial load miss, one independent store
  miss, then two hits to the first block.
- `ex_wb.config`: selects `-w 1`.

## Reference Observation

The prebuilt reference produces:

```text
[0] Address: 0x100 is a Miss
[0] Address: 0x8000 is a Miss
[0] Address: 0x100 is a Hit
[0] Address: 0x100 is a Hit
Ticks - 205
```

This initially appeared to demonstrate foreground hit overlap. Direct probing
later established that `refCache` acknowledges the store early but keeps the
later hit queued until buffered data arrives.

A matching temporary `-w 0` configuration produces the same classifications
in 207 ticks, so mode 1 saves two ticks on this supplied trace.

A direct mock-coherence probe of the prebuilt reference further establishes
the buffered-store acknowledgment boundary. A single-line store miss starts and
calls `permReq` in tick 3, then invokes its processor callback in tick 4
without having received `DATA_RECV`. The write-buffer slot therefore must
retain independent background ownership after the original store callback.
Subsequent corrected probing established the selected scheduling behavior
documented below.

## Incremental Meaning

Before Phase 08, any miss remains the sole active foreground block until
eviction and data acquisition finish. The processor cannot issue its next
request until the original callback, and queued original requests do not start
while an active block waits.

With `-w 1`, an eligible store miss transfers to a separate background slot.
The foreground path can then release that store according to the confirmed
callback timing and inspect later requests. Hits may progress; a later miss
must stop rather than create a second lower-hierarchy foreground transaction.
Mode 1 does not add forwarding reads from the buffer or write coalescing;
those belong to modes 2 and above.

## Terminology Clarification and Review

The student initially asks whether Phase 08 means evicting a cache line into
the write buffer and immediately freeing that line. That is not the behavior
specified by this assignment. `-w 1` buffers the incoming processor store miss,
not merely the dirty data of an evicted victim.

`wb-test.trace` is a concrete counterexample to the eviction interpretation.
With `ex_wb.config`, the first load installs `0x100`; the later store to
`0x8000` maps to a different set in a cache with many invalid ways. It can use
the write-buffer behavior without evicting the `0x100` line or any other valid
line.

If a buffered store miss targets a full set, eviction can be one stage of that
background miss. An asynchronously evicted victim cannot be considered free
or overwritten until its coherence completion arrives, preserving the Phase
04 ordering rule. If an invalid target already exists, no line needs to be
evicted at all. In either case, the final incoming store block is installed
dirty after its required lower-hierarchy work completes.

### Meaning of background execution

Background does not mean an operating-system thread. It means the cache owns a
second persistent state slot independent of foreground `active`. Conceptually,
that slot retains an occupied flag or pointer plus the buffered request's
address, processor, lower-hierarchy status, selected target, possible victim
address, and completion information.

When an eligible store miss transfers from foreground to that slot, foreground
ownership becomes available according to the confirmed callback schedule.
Subsequent cache ticks still call the single coherence tick, which advances the
buffer's outstanding lower transaction. A coherence completion matching the
buffered processor/address updates the background slot and eventually installs
the incoming line dirty. Foreground hits use their own request state and cannot
overwrite the buffered target or status.

The current helpers assume eviction and fill always operate on
`cache_state.active`. Phase 08 therefore requires separating request-specific
miss progress from that single pointer or adding equivalent buffer-specific
handling. Merely placing the request on the existing outer FIFO would not make
it background work, because FIFO entries do not issue or retain an outstanding
coherence transaction.

### Miss data versus eviction completion

The student asks to distinguish `DATA_RECV` from eviction write-back:

- A miss eventually calls `permReq` for the incoming block. `DATA_RECV` for
  that incoming address means its data/permission is available and the cache
  may fill the selected target.
- Replacing a valid victim first calls `invlReq` for the old victim address.
  A matching `FLUSH_COMPLETE` (or the handout's matching `NO_ACTION`) means
  that eviction/flush has completed. Only then does the cache call `permReq`
  for the incoming miss address.
- Conceptually, a dirty victim requires write-back while a clean victim can be
  discarded. In this framework the cache sends no payload bytes and does not
  write memory directly; it invokes the coherence interface, which owns the
  lower-hierarchy transfer.

Thus a full-set miss can have two distinct waits in order: outgoing victim
completion, then incoming `DATA_RECV`. A store miss placed in the Phase 08
buffer is the incoming operation whose state may include that eviction stage;
the buffer is not simply the outgoing victim line.

### Buffered miss does not re-enter the request FIFO

The student asks whether a buffered store miss should wait for `DATA_RECV`,
then return to the original-request FIFO and perform placement or eviction.
It should not: the target and any required victim work belong before the
incoming data wait, following the existing Phase 04 ordering.

The buffered miss retains its selected target and progresses as follows:

```text
select invalid target or full-set victim
if victim is valid:
    issue and complete its eviction
issue permission/data request for the incoming store block
wait for DATA_RECV
install the incoming block in the retained target and mark it dirty
clear the write-buffer slot
```

Returning it to the processor-request FIFO after `DATA_RECV` would duplicate a
request that may already have notified the processor, repeat lookup/replacement,
and risk selecting a different target. The outer FIFO is for later foreground
processor requests; the write-buffer slot independently owns this store miss
until its lower-hierarchy completion.

### Foreground release versus background completion

The student summarizes the buffer as temporary storage while waiting for
`DATA_RECV` and asks whether the operation is otherwise complete. Refined
interpretation:

- From the foreground processor's perspective, the buffered store can be
  acknowledged at the confirmed early completion boundary so later requests
  may be issued.
- From the cache/hierarchy perspective, it is not complete until any required
  victim eviction, incoming permission/data wait, dirty fill, and buffer
  release have finished.
- If an invalid target was available and `permReq` was already issued, then
  most policy decisions are complete and the slot is principally retaining
  ownership while awaiting `DATA_RECV`.
- If a valid victim was selected, the slot may still be waiting for eviction
  before it can even issue the incoming permission request.

The slot also provides capacity/backpressure: while occupied, later misses
cannot create another background transaction, although permitted independent
hits may progress.

## Resolved Design Questions

- A contained one-line store miss is acknowledged one tick after entering the
  buffer, before lower completion.
- The retained request status represents eviction wait or data wait; the
  buffer separately records processor notification and data completion.
- Coherence events match foreground/background expected status, processor, and
  block or victim address.
- After acknowledgment, one later request may leave the outer FIFO and perform
  lookup. A hit remains READY without callback; a miss remains
  `REQUEST_WAITING_BUFFER` without lower work.
- A buffered store includes full-set victim eviction when required.
- Destroy owns the active request, split state, outer FIFO, buffered request,
  and buffer slot.
- A final buffered store may end before background data completion, matching
  the supplied reference's observed three-tick behavior for that trace.

## Student Proposal and Review

Current student decisions and proposals:

- `Confirmed`: only a store miss whose byte range remains within one cache
  block is eligible for the buffer, preserving the existing serial split path
  for multi-block operations.
- `Superseded`: the initial interpretation required natural alignment,
  `address % size == 0`.
- `Confirmed after differential evidence`: for this simulator/reference,
  "unaligned" means an access spanning cache lines. A contained store such as
  `S 0x101,4` is buffer-eligible even though it is not naturally aligned.
- `Confirmed`: use a separate write-buffer FSM with the required empty,
  eviction-wait, and data-wait progress.
- `Reopened`: the student initially selected ordinary next-tick completion for
  independent foreground hits, then requested redesign after recognizing that
  required `wb-test.trace` ends while background work is outstanding.
- `Superseded`: the initial reference-first interpretation left every later
  original request untouched in the outer FIFO while the buffer was occupied.
- `Confirmed from the selected reference behavior`: acknowledge the buffered
  store on the tick after it enters the buffer, before `DATA_RECV`, while the
  buffer retains independent background ownership.
- `Confirmed from the selected reference behavior`: when buffered
  `DATA_RECV` clears the slot, resume the outer FIFO. The direct probe shows
  `refCache` starts/checks the queued request from that completion callback and
  invokes a resulting hit callback on the following tick.
- `Rejected`: waiting for the buffered store's `DATA_RECV` before invoking its
  processor callback conflicts with measured reference behavior.
- `Superseded`: a later miss does not simultaneously await buffer availability
  and its own data. It first remains `REQUEST_WAITING_BUFFER`; only after the
  buffer completes can it issue its own lower request.

Review results:

- Alignment does not mean accessing one complete cache block. The earlier
  natural-alignment interpretation was later disproved by reference behavior;
  the finalized condition is whether the complete byte range remains in one
  cache line.
- Waiting for the buffered store's data before its processor callback defeats
  the required foreground overlap and conflicts with direct reference
  evidence: the reference starts the store miss and calls `permReq` in tick 3,
  then invokes its processor callback in tick 4 before any `DATA_RECV`.
- A later foreground miss cannot wait for its own data yet, because mode 1
  must not issue a second lower miss while the buffer is occupied. It waits
  only for buffer availability. The buffered request's `DATA_RECV` clears the
  slot; the foreground miss can then issue its own permission request and
  enter its own data-wait state.
- The coherence callback carries `(action type, processor number, address)`,
  not the original processor request tag. Request tags exist only for the
  eventual processor completion callback. Event routing must therefore use
  expected state plus processor and block/victim address.
- The student resolves the apparent independent-hit conflict by separating
  lookup/metadata execution from processor callback: the hit executes while
  buffered data is outstanding but its callback waits, matching measured
  external reference timing.

### Interpreting the alignment restriction

The handout says an unaligned access does not buffer but does not define
alignment precisely. A differential trace `S 0x101,4; L 0x100,4` establishes
the supplied reference interpretation: although `0x101 % 4 != 0`, the store is
buffered because its byte range stays inside one cache line. `refCache`
completes in 102 ticks; rejecting buffering under the natural-alignment
interpretation takes 104 ticks.

Therefore Phase 08 uses the already-confirmed one-cache-line condition. A
cross-line store remains on the Phase 05 serial path and is not buffered.

### Why foreground hit callback timing affects termination

The engine loop is driven only by `processor.tick()` progress. The cache's own
tick return value is ignored. After the buffered store is acknowledged early,
the processor can issue later hits. If every one of those hits also callbacks
immediately and the trace reaches EOF while the buffered store still awaits
data, the processor can report no remaining progress and terminate the
simulation before background completion.

On the supplied trace, an immediate-release model would allow both final hits
and EOF long before the roughly 100-cycle buffered miss, yet `refCache`
continues to 205 ticks.

A corrected direct probe used a mock permission interface that returns
immediate permission for the known resident hit. The reference behavior was:

```text
tick 3: start buffered store miss and issue its permission request
tick 4: callback the store before DATA_RECV
ticks 5--7: keep the later resident load queued; no hit permission/callback
tick 7: inject the buffered store's DATA_RECV
tick 7: only now request/check permission for the queued resident load
tick 8: callback that hit
```

Thus the supplied `refCache` does not process even an independent queued hit
while its mode-1 buffer is waiting for data. This resolves its 205-tick
liveness behavior, but conflicts with the handout sentence that independent
read/write hits can execute until a second access misses. The implementation
target requires an explicit student decision or instructor clarification.

### Observable benefit under the selected reference behavior

The student asks what benefit remains if all later requests wait behind an
occupied buffer. The measured reference behavior still acknowledges the store
miss early, allowing the processor to submit its next request into the outer
FIFO before the store data arrives. When the buffer completes, that already
queued request can start immediately from the completion path.

Without buffering, the processor receives the store callback only after data
completion, submits its next request afterward, and that request starts on a
later cache tick. This explains the narrow measured improvement on
`wb-test.trace`: 207 ticks with `-w 0` versus 205 with `-w 1`.

This reference behavior overlaps acknowledgment/queueing but not actual
foreground hit execution. The handout describes a stronger intended benefit,
where independent hits execute during the buffered miss. The discrepancy
remains documented even though the student provisionally selected reference
compatibility.

### Reconsidered target and last-hit limitation

The student reconsiders the provisional reference-first choice and prefers the
handout's independent-hit behavior, with special handling only when the final
trace operation is a hit and buffered data has not arrived.

Review: the cache cannot identify that condition through its current
interfaces. It owns neither the trace reader nor processor EOF state. When it
callbacks a hit, control returns to `processor.tick`, which may discover EOF
and return zero immediately. The engine loop is driven only by that processor
return value and then stops calling cache/coherence ticks. The cache learns
about shutdown only through `finish` after the main loop has ended.

Possible mechanisms and limitations:

- Let every independent hit callback normally: faithful to the handout, but a
  final hit can end the simulation with buffered work outstanding.
- Drain the buffer from `cache.finish`: can complete host-side state but those
  lower ticks do not increment the processor's reported simulated tick count,
  so reference timing cannot match.
- Retain a foreground hit callback while the buffer is occupied: keeps the
  processor pending and matches observed reference liveness, but is not
  special to the final trace hit because the cache cannot know which hit is
  final.
- Change processor/engine progress accounting to include cache background
  work: architecturally clean, but outside the assignment's cache-subdirectory
  boundary.

Therefore “normal callback except for the final hit” is not implementable
cleanly inside `teamCache` without an additional EOF/progress signal. This
specification/framework conflict requires either an instructor ruling or an
explicit trade-off selected by the student.

Superseded student decision: prioritize the supplied `refCache` and
required `wb-test.trace` timing. While the mode-1 buffer is occupied, retain
all later original requests in the outer FIFO without lookup. A buffered store
is acknowledged on the tick after it enters the slot; its lower work continues
in the background. When buffered data completes, clear the slot and resume the
queued foreground path according to the measured reference transition. This
supersedes the reconsidered independent-hit proposal above while preserving
the conflict as documented rationale.

### Refined interpretation of independent hit execution

The student proposes that the handout's word "execute" can mean performing the
cache lookup and hit-side metadata work without immediately invoking the
processor callback. This is consistent with a previously observed reference
debug snapshot: while the buffer was occupied, the foreground request was
shown with `Request Status: Hit`, but its callback was not released until the
buffer completed.

Under this refinement, the request should not remain entirely untouched in the
outer FIFO:

- Start one foreground request while the buffer is occupied.
- If it hits, perform normal hit metadata/dirty updates and retain it in a
  ready-but-buffer-blocked condition without callback.
- If it misses, issue no eviction or permission request; retain it in a
  buffer-wait condition.
- When buffered data completes, a retained hit becomes eligible for its normal
  callback boundary; a retained miss can begin its ordinary miss path.

This preserves the measured external `refCache` timing and engine liveness
while giving "independent hit can execute" a concrete internal meaning. It
supersedes the stronger statement that every later request remains uninspected
in the outer FIFO.

Final student decision (`Confirmed` and `Implemented`): adopt this refined
interpretation. One foreground request may perform lookup while the buffer is
occupied. A hit updates replacement/dirty metadata and remains READY without
callback; a miss remains `REQUEST_WAITING_BUFFER` without lower work. Buffered
completion releases the hit for the next tick or starts the waiting miss.

## Confirmed Implementation Contract

```text
memoryRequest:
    copy every arrival into the existing outer FIFO

when a cache tick starts one original request:
    use the existing split and lookup path
    if it is not an eligible mode-1 store miss:
        retain the normal foreground behavior
    otherwise:
        require exactly one covered cache block
        transfer the active block to the empty write-buffer slot
        select/reserve its target
        begin eviction if required, otherwise issue its permission request

on the next cache tick after buffering:
    callback the original store exactly once
    release its one-block foreground completion record
    retain the request in the write-buffer slot for background progress

while the write-buffer slot is occupied:
    after the store acknowledgment, allow one outer-FIFO request to start
    if it hits:
        update hit metadata and retain READY without callback
    if it misses:
        retain REQUEST_WAITING_BUFFER without eviction or permReq
    continue ticking coherence

on matching buffered eviction completion:
    invalidate the retained victim target
    issue the buffered incoming permission request

on matching buffered DATA_RECV:
    fill the retained target and mark it dirty
    clear and free the write-buffer slot
    if foreground is READY:
        leave its callback for the following cache tick
    else if foreground is REQUEST_WAITING_BUFFER:
        look up that waiting request again
        a hit updates metadata and becomes READY without a same-tick callback
        a miss starts its ordinary miss path now
    else:
        immediately start the current outer-FIFO head, if present

destroy:
    release foreground, inner/outer queues, completion state, and buffer state
```

The write-buffer entry owns its retained `cache_request`, lower wait status,
target/victim information, processor identity, and flags distinguishing early
processor notification from background data completion. Coherence events have
no request tag and are matched by expected status, processor, and block/victim
address.

## Implementation Mapping

- `teamCache/write_buffer.h/.c`: eligibility, active-request adoption, owned
  slot state, and destruction.
- `teamCache/cache_internal.h`: one optional `cache_write_buffer*`.
- `teamCache/access.c`: buffer transfer, early acknowledgment, event routing,
  dirty fill, FIFO blocking, and completion-path resume.
- `teamCache/eviction.h/.c`: explicit request argument shared by foreground
  and buffered eviction paths.
- `teamCache/split.h/.c`: release the one-block processor completion record
  without freeing the background-owned request.
- `teamCache/CMakeLists.txt`: compile `write_buffer.c`.
- `tests/teamCache/phase08_write_buffer_test.c`: early callback, retained
  background ownership, early hit lookup with delayed callback,
  `REQUEST_WAITING_BUFFER` miss resume, same-line wait becoming a hit after
  the buffered fill, immediate `permReq` grant fill, delayed dirty eviction,
  contained-address eligibility, split exclusion, `-w 0`, and cleanup.
- `teamCache/access.c:trace_access`: later split blocks now print their actual
  hit/miss/eviction outcome instead of a hard-coded `Hit`.

## Acceptance Behaviors

- `-w 0` preserves Phase 01--07 behavior and reference timing.
- One contained store miss occupies the buffer without losing address or
  completion state.
- Independent read and write hits progress while the buffer is occupied.
- A subsequent miss waits and later resumes without loss, duplication, or
  lower-request overlap.
- A subsequent request for the same in-flight buffered line becomes a hit
  after the fill, without a second permission request.
- If `permReq` grants permission immediately, the miss fills in place and
  remains READY for the following tick's processor callback.
- A cross-line store miss follows the ordinary blocking path.
- Foreground and buffered callbacks/events cannot overwrite one another.
- Destroying with an occupied buffer releases all cache-owned storage.
- `wb-test.trace` matches `refCache` verbose output and 205 ticks.

## Commands and Observed Results

```sh
cmake -S . -B /tmp/cadss-phase08-build -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build /tmp/cadss-phase08-build --target teamCache cadss-engine trace \
  processor branch coherence interconnect memory -j 4

gcc -std=c11 -g -O0 -Wall -Wextra -Wpedantic -Werror \
  -Icommon -IteamCache tests/teamCache/phase08_write_buffer_test.c \
  teamCache/cache.c teamCache/access.c teamCache/eviction.c \
  teamCache/replacement.c teamCache/request_queue.c teamCache/split.c \
  teamCache/write_buffer.c teamCache/lookup.c teamCache/lifecycle.c \
  -o /tmp/phase08_write_buffer_test
/tmp/phase08_write_buffer_test

valgrind --leak-check=full --show-leak-kinds=all \
  --errors-for-leak-kinds=all --error-exitcode=99 \
  /tmp/phase08_write_buffer_test
git diff --check
```

- Component/framework builds pass.
- Phase 01--08 harnesses pass under strict compilation.
- The Phase 08 harness covers the implementation mapping above.
- Valgrind reports 79 allocations, 79 frees, zero bytes at exit, and zero
  errors.
- `wb-test.trace` matches `refCache` verbose output and 205 ticks exactly.
- `traces/cache/trans.trace` with `ex_wb.config` now matches `refCache` at
  1651 ticks and identical verbose classifications. The previous path treated
  a same-line `REQUEST_WAITING_BUFFER` resume as a miss and aborted when
  `permReq` returned true.
- A contained non-naturally-aligned trace `S 0x101,4; L 0x100,4` matches
  reference classifications and 102 ticks, establishing single-line
  eligibility.
- A cross-line trace `S 0xf,2; L 0x0,1` remains unbuffered and matches
  reference verbose output and 204 ticks after fixing the later-block outcome
  label.
- A final-store-only mode-1 trace matches `refCache` at 3 ticks.
- `-w 0` focused hit timing remains 104 ticks and RRIP timing remains 316.
- `git diff --check` passes.

## Split Cursor (2026-09-17)

A 32-byte store with 16-byte blocks still has `completion->total == 2` and is
not write-buffered. The second block is no longer a pre-queued `cache_request`
on `queue_head`. Buffered tests seed the dirty victim with `cache_ensure_line`.
