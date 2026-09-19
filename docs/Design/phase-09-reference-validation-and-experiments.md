# Phase 09: Reference Validation and Experiments

## Status

- Design: Open; experiment ranking decisions belong to the Phase 09 owner
- Implementation: Not started
- Acceptance: Not run

## Active Investigation: Gradescope bzip2 Differential

The student reported partial credit (`6.95/9.09`) for both
`cache/lru/bzip2` and `cache/rrip/bzip2`: `teamCache` completes the same
work with a small tick-count difference from `refCache`. The exact hidden
configuration, trace, verbose output, and expected/observed tick counts are
not visible in the repository, and the linked Gradescope result requires an
authenticated session that is unavailable to the agent.

Current scope is diagnostic only: infer likely hidden scenarios from the test
names, construct reference-differential traces, reduce any mismatch to an MRE,
and explain the responsible state/timing transition. No cache-policy change is
authorized by this investigation alone.

Evidence and hypotheses to check:

- `Open`: because LRU and RRIP receive the same partial score, first inspect
  shared request, split-access, eviction, and callback timing before blaming
  policy-specific victim selection.
- `Open`: commit `27c40e0` claims both policies match a local real
  `bzip2.trace` at 1,559,426 ticks, but that trace and its exact configuration
  are absent here, so the claim is not yet independently reproducible.
- `Open`: the new full-set/later-split-block special case in
  `teamCache/access.c` deliberately reports an instantaneous no-op hit. It may
  match one reference quirk while hiding a different state transition on
  subsequent accesses.
- `Open`: hidden tests may use a configuration different from the one behind
  the 1,559,426-tick claim, including associativity, write-buffer mode, or RRIP
  width.

The reusable probe is `experiments/scripts/differential_probe.py`. It compares
exit status, total ticks, and normalized verbose access classifications for the
same generated trace under both simulators, then greedily removes operations
from mismatching traces while preserving the mismatch.

### Reproduced MREs and Version Split

Repository state at investigation time:

- `origin/submission` is still `1041e4b`; this is the likely implementation
  represented by the existing Gradescope result, although the submission page
  did not expose its uploaded commit.
- `origin/develop` is `27c40e0`, Ziqi's later replacement of `teamCache`.
  This version has not been merged into `submission` or `master`.

`tests/teamCache/bzip2_split_full_set.trace` fills both ways of set zero and
then issues a two-line access whose second block maps to that full set:

```text
L 0,1
L 40,1
L 1f,2
```

Both `bzip2_split_lru.config` and `bzip2_split_rrip.config` reproduce the same
policy-independent result:

| implementation | total ticks | final split classification |
| --- | ---: | --- |
| submitted `1041e4b` | 505 | `0x20 is also a Evict` |
| current `27c40e0` | 305 | `0x20 is also a Hit` |
| `refCache` | 404 | `0x20 is also a Hit` |

This is a strong local analogue for a submitted simulator modestly exceeding
the reference tick count on both LRU and RRIP. The old implementation performs
an ordinary eviction and fill for the later split block; the reference spends
approximately one lower-level round trip less.

Appending `L 20,1` (`bzip2_split_full_set_followup.trace`) gives 507 ticks for
both old and current `teamCache`, versus 505 for `refCache`. The classifications
also reveal different retained state: old `teamCache` calls the follow-up a
hit, current `teamCache` calls it an eviction, and `refCache` calls it an
eviction. The evidence is consistent with this reference-specific sequence:

1. choose/invalidate a victim for the later split block;
2. wait for that invalidation;
3. label the block as `also a Hit` and complete the original access without a
   normal replacement fill;
4. retain cache metadata that makes the later independent access attempt an
   eviction again, while coherence already considers the victim invalid.

Step 3/4 is an inference from differential timing and classifications, not a
documented assignment rule. Disassembly confirms that `refCache` has a
dedicated `twoBlocks` field/path, but does not by itself make this behavior a
required architecture policy.

The special case added in `27c40e0` (`teamCache/access.c`, the
`block_index != 0 && !has_room` branch) skips both eviction and fetch and marks
the subrequest ready immediately. It fixes the verbose label and changes the
old +101-tick isolated error into a -99-tick error; it does not reproduce the
reference state transition. The source comment reports exact aggregate ticks
on the author's unavailable real `bzip2.trace`, so this may be a trace-specific
compensation rather than a generally correct model.

### Controls and Secondary Hypothesis

- The local 286,966-access Cache Lab `long.trace` contains zero accesses that
  cross a 16-byte line. With `s=8,E=16,b=4,i=0,w=0`, current `teamCache` and
  `refCache` match exactly: 1,800,837 LRU ticks and 1,802,237 RRIP ticks.
  This control strengthens the split-access hypothesis but cannot substitute
  for the unavailable official `bzip2.trace`.
- The handout's sample `ex_rrip.config` contains `-i 4`, even though Fall 2026
  says victim cache is not evaluated. On local `long.trace`, current
  `teamCache` (which intentionally ignores `-i 4`) takes 1,802,237 ticks while
  `refCache` takes 1,801,124. If the hidden bzip2 test copied that sample flag,
  unsupported victim-cache behavior is a second possible source. The course
  specification makes this less likely than the split-path explanation.
- A real resolution needs the exact Gradescope configuration and official
  `bzip2.trace`, or at least their verbose/tick outputs. Without them, do not
  claim that either the old complete-fill model or the new instant-hit model
  is globally reference-equivalent.

### Investigation Commands and Results

- Built `27c40e0` in `/tmp/cadss-bzip-build`; required component targets pass.
- Archived and independently built `origin/submission` (`1041e4b`) in
  `/tmp/cadss-submission-old` without changing the checked-out branch.
- Ran the four checked-in split traces against LRU and RRIP configurations for
  old `teamCache`, current `teamCache`, and `refCache`; the results above repeat
  for both policies.
- Ran `experiments/scripts/differential_probe.py` with 300 seeded generated
  cases per valid configuration. It independently reduced discrepancies to
  crossing-line requests and found the same classification/state differences
  under both LRU and RRIP when invalid ways remain.
- The linked Gradescope page could not be read without its authenticated
  session; no hidden expected/actual output was assumed.

### Experimental Optimization Branch

`Confirmed` by the student's request: branch
`fix/bzip2-split-reference-timing` will model the inferred reference behavior
instead of either the submitted full fill or `27c40e0`'s instantaneous no-op.
This is an explicitly reference-compatibility behavior for a later block of a
split access whose destination set is full; ordinary accesses and split blocks
with an invalid target retain their existing behavior.

Confirmed behavioral pseudocode:

```text
on a miss for block_index > 0 when the destination set is full:
    select the policy's normal victim
    reconstruct and retain the victim address
    report the split block as Hit, matching refCache output
    issue invlReq for the victim
    if invalidation is asynchronous:
        wait in a distinct split-eviction-only state
    when invalidation is complete:
        preserve the victim line's cache metadata as-is
        do not issue permReq and do not install the requested block
        mark the split block ready for normal next-tick retirement
```

The distinct state is required so a normal eviction acknowledgement still
invalidates the victim and starts a fetch, while this compatibility path does
neither. RRIP victim selection may perform its normal aging while searching;
no replacement hit/fill update is applied afterward. Acceptance targets are
404 ticks for `bzip2_split_full_set.trace` and 505 ticks for
`bzip2_split_full_set_followup.trace`, under both checked-in LRU and RRIP
configurations, with the same verbose classifications as `refCache`.

`Implemented` on the experimental branch:

- `teamCache/access.h` adds `REQUEST_WAITING_SPLIT_EVICTION` so the event
  handler can distinguish this path from an ordinary eviction.
- `teamCache/access.c::begin_split_eviction_only` selects the normal LRU/RRIP
  victim, sends its invalidation, handles immediate completion, and deliberately
  leaves the line metadata unchanged.
- `cache_access_event` recognizes asynchronous acknowledgements for the new
  state and transitions directly to `REQUEST_READY` without calling
  `cache_eviction_complete` or `fetch`.
- `tests/teamCache/bzip2_split_reference_test.py` compares complete verbose
  output and ticks with `refCache` for LRU and RRIP across delayed invalidation,
  clean and dirty victims, later independent reuse, and repeated split
  access/immediate invalidation.

Observed acceptance results:

| trace | LRU team/ref | RRIP team/ref |
| --- | ---: | ---: |
| `bzip2_split_full_set.trace` | 404 / 404 | 404 / 404 |
| `bzip2_split_dirty_full_set.trace` | 404 / 404 | 404 / 404 |
| `bzip2_split_full_set_followup.trace` | 505 / 505 | 505 / 505 |
| `bzip2_split_repeat.trace` | 407 / 407 | 407 / 407 |

The focused test passes all eight comparisons. Existing engine controls remain
exact: Phase 03 one-hit is 104 ticks, Phase 04 clean eviction is 506 ticks,
local `long.trace` LRU is 1,800,837 ticks, and local `long.trace` RRIP is
1,802,237 ticks for both simulators.

The historical Phase 01/02/05--08 harness sources do not compile against
`27c40e0`'s replaced lazy-storage/split/write-buffer interfaces. Phase 03 and
Phase 04 compile but fail identically on an archived unmodified `27c40e0`
baseline and on this branch. These are pre-existing test/implementation drift,
not regressions introduced by the split-eviction state. They remain a Phase 09
test-maintenance follow-up; the engine-level controls above are authoritative
for this scoped optimization.

## Purpose of This Handoff

This document hands the completed cache implementation to the teammate
responsible for Phase 09 validation, parameter exploration, and the experiment
report. Earlier design history, student decisions, tests, known discrepancies,
and observed reference behavior are under `docs/Design/`.

## Branch Layout: Read Before Starting

The branches intentionally contain different handoff material:

- `master` will receive only the completed `teamCache/` implementation needed
  for the final cache submission.
- The full `docs/Design/` history, this Phase 09 handoff, validation notes, and
  experiment guidance are maintained on `develop`.
- Commit hashes listed in this document refer to `develop`.

The Phase 09 owner must switch to and update `develop` before reading the
design records:

```sh
git status
# Save or commit local work before switching if the tree is not clean.
git switch develop
git pull origin develop
```

Do not conclude that a design record is missing merely because it is not
present on `master`. For implementation-only submission work, use the
`teamCache/` content synchronized to `master`. For Phase 09 investigation and
report preparation, use `develop`, which already contains both the same
implementation history and its documentation.

Because only `teamCache/` was synchronized, `master` does not contain
`develop`'s root `CMakeLists.txt` registration or `submission` update. The
normal whole-project build command below therefore applies directly on
`develop`; a standalone `master` validation must compile/link `teamCache/`
explicitly or place it into the course submission framework.

Do not infer current behavior only from old discussion sections. In each phase
record, read the status, latest confirmed contract, implementation mapping, and
latest observed results. Historical proposals are retained and marked
`Superseded`, `Rejected`, or `Reopened` where applicable.

## Assignment Goal

Phase 09 must:

- validate `teamCache` systematically against `refCache`;
- exercise LRU, RRIP, access sizes, split accesses, eviction, request queueing,
  and `-w 1`;
- run memory-safety checks;
- enumerate cache candidates under the 54KB modeled-storage budget;
- run every official experiment trace for every retained candidate;
- select one configuration for all experiment traces using a documented AAT
  comparison method;
- preserve raw data, derived results, commands, and report claims so another
  person can reproduce them.

Victim cache and subblocking are Fall 2026 non-goals. Omit `-i` and `-u`, or
set both to zero, in fair experiment and differential configurations.

## Implementation Phase Index

- Phase 00, team component scaffold:
  `phase-00-project-scaffold.md`; `teamCache/CMakeLists.txt`, root registration,
  and `submission`.
- Phase 01, initialization, arguments, storage, cleanup:
  `phase-01-initialization-and-lifecycle.md`; `teamCache/cache.c`,
  `lifecycle.c/.h`, and `cache_internal.h`.
- Phase 02, 64-bit set/tag decoding and hit lookup:
  `phase-02-address-decoding-and-lookup.md`; `teamCache/lookup.c/.h`.
- Phase 03, queue-first timing, hits, misses, fills, and dirty state:
  `phase-03-lru-hits-misses-and-fills.md`; `teamCache/access.c/.h`.
- Phase 04, set-local eviction and asynchronous flush ordering:
  `phase-04-eviction-and-dirty-write-back.md`; `teamCache/eviction.c/.h`.
- Phase 05, variable sizes and low-address-first split accesses:
  `phase-05-variable-size-and-split-line-accesses.md`;
  `teamCache/split.c/.h`.
- Phase 06, LRU/RRIP metadata and victim policy:
  `phase-06-rrip-replacement.md`; `teamCache/replacement.c/.h`.
- Phase 07, FIFO of complete original processor requests:
  `phase-07-request-queueing.md`; `teamCache/request_queue.c/.h`.
- Phase 08, one mode-1 buffered store miss:
  `phase-08-single-entry-write-buffer.md`; `teamCache/write_buffer.c/.h` and
  integration in `access.c`.

### Central runtime state

`teamCache/cache_internal.h` defines:

- allocated sets and lines;
- cache dimensions and selected replacement/write-buffer modes;
- one foreground active block;
- the Phase 05 inner block FIFO and completion count;
- the Phase 07 outer original-request FIFO;
- one optional Phase 08 background write-buffer slot.

`teamCache/access.c` is the central state-transition coordinator. It starts
queued work on cache ticks, performs lookup, delegates replacement/eviction,
routes coherence events, and invokes processor callbacks. `cache.c` remains
the small public framework adapter.

## Current Behavioral Contracts

### Request timing

The instructor clarified that `memoryRequest` only queues an arrival. The
cache starts it at the beginning of a later cache tick. If request A completes
in tick T, the next original request starts in tick T+1.

The focused miss/hit trace matches `refCache` at 104 ticks after this revision.

### Split accesses

An original access covers the inclusive range:

```text
address .. address + size - 1
```

Its cache-line parts execute serially from low address to high address and
produce exactly one original processor callback. A later split block now
prints its actual hit/miss/eviction outcome rather than a hard-coded `Hit`.

### Replacement

- LRU is the default.
- `-R <k>` selects static RRIP.
- Both policies prefer the first invalid way.
- RRIP hit value: `0`.
- RRIP insertion value: `2^k - 2`.
- RRIP victim: first way at `2^k - 1`; age the full set when none exists.
- `k=64` is handled without shifting by 64.

### Mode-1 write buffer

The implemented interpretation matches measured `refCache` timing while
allowing an independent foreground lookup:

- a contained single-cache-line store miss can enter the buffer;
- it callbacks the processor on the following tick, before `DATA_RECV`;
- one foreground request may perform lookup while buffered work is pending;
- a hit updates metadata and remains READY, but its callback waits for buffer
  completion;
- a miss remains `REQUEST_WAITING_BUFFER` and issues no lower request;
- buffered eviction/data state is separate from foreground state;
- buffered completion releases the hit or starts the waiting miss.

The handout's word "unaligned" is interpreted by the supplied reference as
crossing cache lines, not failure of natural alignment. The contained trace
`S 0x101,4; L 0x100,4` buffers and matches reference at 102 ticks.

## Source and Test Map

### Implementation

- `teamCache/cache.c`: framework callbacks and lifecycle entry points.
- `teamCache/cache_internal.h`: line/state definitions.
- `teamCache/lifecycle.c/.h`: option parsing, dimensions, allocation, cleanup.
- `teamCache/lookup.c/.h`: set/tag lookup.
- `teamCache/access.c/.h`: foreground and background state transitions.
- `teamCache/eviction.c/.h`: invalid target/victim and flush ordering.
- `teamCache/split.c/.h`: current original request's block FIFO.
- `teamCache/replacement.c/.h`: LRU and RRIP.
- `teamCache/request_queue.c/.h`: outer original-request FIFO.
- `teamCache/write_buffer.c/.h`: mode-1 buffer eligibility and ownership.

### Focused harnesses

- `tests/teamCache/phase01_lifecycle_test.c`
- `tests/teamCache/phase02_lookup_test.c`
- `tests/teamCache/phase03_access_test.c`
- `tests/teamCache/phase04_eviction_test.c`
- `tests/teamCache/phase05_split_test.c`
- `tests/teamCache/phase06_rrip_test.c`
- `tests/teamCache/phase07_queue_test.c`
- `tests/teamCache/phase08_write_buffer_test.c`

The exact strict-compilation commands and latest Valgrind results are recorded
in the corresponding phase documents. Phase 08's harness currently reports
79 allocations, 79 frees, zero bytes at exit, and zero Valgrind errors.

## Known Results and Follow-Ups

Passing reference comparisons:

- Phase 03 miss/hit trace: 104 ticks for both caches.
- Phase 04 clean conflict: 506 ticks for both.
- Phase 04 dirty conflict: 506 ticks for both.
- `traces/cache/wide.trace`: matching verbose output and 2136 ticks.
- RRIP `traces/cache/load.trace`: three misses, six hits, 316 ticks for both.
- `traces/cache/wb-test.trace`: matching verbose output and 205 ticks.
- Contained non-naturally-aligned buffered store trace: 102 ticks for both.
- Cross-line unbuffered store trace: matching verbose output and 204 ticks.
- Final buffered store trace: 3 ticks for both.

Items Phase 09 must revisit:

1. The Phase 05 `E=1` split-eviction discrepancy was re-run from final
   `master` commit `bbfe7b0` and persists: `teamCache` reports the later block
   as `Evict` at 305 ticks; `refCache` reports it as `Hit` at 204 ticks. Resolve
   the specification/reference conflict before claiming complete differential
   compatibility.
2. Add a reference-comparable RRIP conflict trace that forces aging and victim
   selection; the current RRIP reference trace does not force every internal
   rule.
3. The handout says mode-1 independent hits can execute. The implemented
   interpretation performs lookup/metadata early but retains callback until
   buffered completion, matching `refCache` and preventing premature engine
   termination. Describe this interpretation explicitly in the report.
4. Test combinations, not only isolated features: RRIP plus split eviction,
   RRIP plus `-w 1`, queued multi-processor identities, and buffered full-set
   dirty eviction.

## Build and Runtime Procedure

From the repository root:

```sh
cmake -S . -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build --target teamCache cadss-engine trace processor branch \
  coherence interconnect memory -j 4
```

The engine constructs component library paths relative to its working
directory. Run from the build directory:

```sh
cd build

/home/chenyy/cadss_public/cadss-engine \
  -s /absolute/path/to/config \
  -c teamCache \
  -t /absolute/path/to/trace

/home/chenyy/cadss_public/cadss-engine \
  -s /absolute/path/to/config \
  -c /home/chenyy/cadss_public/refCache \
  -t /absolute/path/to/trace
```

Add `-v` for event classifications. The reference is the prebuilt binary
`refCache/librefCache.so`; it is not a CMake target.

Minimal experiment configuration:

```text
__processor
__cache -s <s> -E <E> -b <b> -w <0-or-1> [-R <k>]
__branch
```

Do not reuse sample `-i 4` settings for fair comparisons: `teamCache`
intentionally ignores nonzero victim-cache settings while `refCache` may not.

## Phase 09 Validation Matrix

Before parameter exploration, build a compact matrix that isolates:

- LRU and RRIP;
- RRIP `k` boundary and ordinary values;
- `-w 0` and `-w 1`;
- 1-, 2-, 4-, and 8-byte aligned and contained-misaligned accesses;
- first-byte/last-byte cache-line boundaries;
- clean and dirty full-set eviction;
- immediate and delayed eviction completion;
- cold miss, repeated hit, and conflict miss;
- multiple queued original requests with distinct processor/tag identities;
- buffered store with invalid target and with dirty victim;
- second foreground hit and second foreground miss while buffering;
- high 64-bit addresses and rejected inclusive-range overflow.

For each case:

1. use the same configuration and trace with `teamCache` and `refCache`;
2. capture exit status, verbose output, and total ticks separately;
3. compare verbose classifications and ordering;
4. compare total ticks;
5. reduce every difference to a minimal trace before changing code;
6. record whether the result passes, fails, or reflects a documented
   specification/reference conflict.

## 54KB Modeled-Storage Filter

Do not use host `sizeof(cache_line)` as the modeled hardware cost. C padding,
pointers, and simulator-only ownership structures are not automatically the
assignment's hardware storage model.

For each candidate:

```text
S = 2^s
B = 2^b bytes
L = S * E lines
tag_bits_per_line = 64 - s - b

data_bits = L * B * 8
tag_bits = L * tag_bits_per_line
valid_dirty_bits = L * 2
replacement_bits =
    L * k                 for RRIP
    L * <documented e>    for LRU

base_cache_bits =
    data_bits
  + tag_bits
  + valid_dirty_bits
  + replacement_bits
```

Then add the modeled write-buffer storage required by the selected `-w`
configuration and reject candidates above:

```text
54 * 1024 * 8 bits
```

Two storage-model details are not resolved by the handout text alone and must
be explicitly decided/documented by the Phase 09 owner:

- what the handout means by “LRU uses e-bits per line”;
- which data/address/control fields count as the required mode-1 write-buffer
  hardware overhead.

Do not silently choose these formulas. Check course clarification if available,
state the interpretation in the report, and keep the calculation script beside
the raw results.

## Candidate Parameter Search

Enumerate only configurations that pass the storage filter:

- `b`: assignment range 4 through 10;
- `s`: nonnegative and satisfying `s + b <= 64`;
- `E`: positive integer, bounded by the 54KB budget;
- replacement: default LRU or RRIP;
- RRIP `k`: supported range 1 through 64, narrowed to a documented candidate
  set for practical search;
- `w`: 0 or required mode 1;
- `i=0`, `u=0`.

Recommended mechanical workflow:

1. Generate a manifest containing one row per candidate and all derived
   storage terms.
2. Reject over-budget candidates before simulation.
3. Generate one config file per retained candidate from the manifest.
4. Run every retained candidate on every official experiment trace.
5. Save raw stdout/stderr, exit status, elapsed host time, and parsed ticks.
6. Count or otherwise record the number of measured memory accesses per trace.
7. Derive per-trace AAT using the teammate-confirmed formula.
8. Rank candidates using the teammate-confirmed cross-trace aggregation rule.
9. Re-run the winning and nearby configurations to catch parsing or transient
   mistakes.
10. Use one selected configuration for all experiment traces, as required.

Suggested durable layout:

```text
experiments/
  search-config.json
  candidates.csv
  configs/
  raw/
    teamCache/
    refCache/
  derived/
  scripts/
  README.md
```

Do not overwrite raw outputs when deriving tables. Scripts should read raw
files and produce derived CSV/plots deterministically.

## AAT and Cross-Trace Selection: Owner Decisions Required

The assignment asks for lowest AAT and one configuration across all traces but
does not fully specify the aggregation method in the handout text available
here. Before running the full search, the Phase 09 owner must confirm and
record:

1. the denominator used for each trace's AAT;
2. whether trace warm-up or initialization ticks are included;
3. how several per-trace AAT values become one ranking score;
4. whether traces receive equal weight or are weighted by access count;
5. deterministic tie-breaking between equal candidates.

Possible methods must be evaluated by the teammate rather than silently
selected in this handoff. Whatever method is chosen must be used consistently
in scripts, tables, configuration selection, and report prose.

## Report Evidence Checklist

The report should be backed by saved artifacts for:

- module/state organization and why foreground/background ownership is
  separated;
- LRU versus RRIP behavior;
- split-access and request-queue ordering;
- write-buffer interpretation and measured reference behavior;
- modeled-storage formula and 54KB proof for the chosen configuration;
- candidate ranges and number filtered/run;
- per-trace access counts, ticks, and AAT;
- cross-trace ranking and tie-breaking;
- the single selected configuration;
- focused correctness/reference tests;
- sanitizer/Valgrind results;
- known discrepancy or instructor clarification, if any remains.

Every numeric claim should be traceable to a raw file and derivation script.

## Suggested First Actions for the Phase 09 Owner

1. Switch to and pull `develop`; the design records are not being copied to
   `master`.
2. Read this file, `phase-roadmap.md`, and Phase 05/06/08 records in full.
3. Rebuild from a clean build directory.
4. Run all eight focused harnesses.
5. Re-run the known reference comparisons listed above.
6. Resolve the two modeled-storage questions.
7. Confirm the AAT and cross-trace aggregation method.
8. Implement the candidate generator and storage filter.
9. Run a small sample matrix and inspect raw artifacts before launching the
   full search.
10. Resolve or explicitly report the confirmed Phase 05 split-eviction
    reference conflict.
11. Mark this phase `Implemented` only when scripts/results exist and
    `Accepted` only when all roadmap acceptance behaviors pass.

## Commands and Observed Results

- Handoff created after Phase 08 commit `a590e06`.
- No Phase 09 experiment scripts, candidate manifests, or report results exist
  yet.
- The implementation is on both `develop` and `master`; design documentation
  remains on `develop`.
- `teamCache/` was copied without `docs/` to `master` and pushed as commit
  `bbfe7b0`.
- On that `master` commit, all Phase 01--08 strict C11 harnesses pass. All
  eight harnesses also pass Valgrind with leak/error failures enabled.
- Reference checks match for the Phase 03 hit case, clean/dirty Phase 04
  conflicts, set-local LRU, `wide.trace`, RRIP `load.trace`, and
  `wb-test.trace`. The Phase 05 discrepancy above is the remaining checked
  mismatch.

## 2026-09-17 Split Cap, Cursor, and Lazy Storage

Student-directed implementation changes on `develop`:

- Split work stops after two consecutive lines.
- Split state is a cursor (`first_block`/`last_block`), not N queued
  `cache_request` objects.
- Cache sets and lines grow on demand.

These are intended to match the reference's large-`size` wall-clock behavior
while keeping invalid full-cache overflow checks.
