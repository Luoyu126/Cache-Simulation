# Phase 01: Initialization and Lifecycle

## Status

- Design: Confirmed for Phase 01 (student design plus delegated mechanics)
- Implementation: Implemented
- Acceptance: Accepted for Phase 01 in the local environment

The component target, lifecycle checks, and real-engine empty-trace runs pass.
Full-project compilation still requires the missing zlib development headers;
framework-owned leaks are recorded separately below. No access behavior has
been accepted by this phase.

## Scope and Non-Goals

Scope and expected acceptance behavior are defined by
`phase-roadmap.md#phase-01-initialization-and-lifecycle`.

This phase owns configuration parsing, derived dimensions, simulator-owned
cache storage, public interface initialization, coherence callback
registration, and cleanup. It does not perform cache accesses, issue coherence
requests, or make replacement decisions.

## Relevant Specification and Code

- Assignment handout: `docs/346f26 P1-cache.pdf`, Sections 3 and 4
- Public cache interface: `common/cache.h`
- Public coherence interface: `common/coherence.h`
- Starter lifecycle functions: `teamCache/cache.c`
- Reuse reference: `cachelab-m25-ziqibang/csim.c`, especially its block
  structure, allocation routine, parameter parsing, and cleanup routine

## Dependencies

- Phase 00 provides the registered `teamCache` component and build target.
- Later phases depend on the storage layout and ownership rules chosen here.

## Current Implementation Contract

The student's instruction to execute Phase 01 accepts the discussed flat file
layout and lifecycle plan. The history below preserves earlier proposals and
open questions; this section records their current resolution before coding.

### Confirmed student design

- `cache.c` owns one file-local global `cache_state` object. Internal functions
  receive its address; the shared header defines types, not global objects.
- `cache_state` retains `sets`, `s`, `E`, `b`, `S`, and `B`. `sets` is a
  `cache_line***`: each of S sets has E pointer slots, each pointing to its own
  separately allocated line. There is no simulated data payload.
- Line valid state, tag, last-access timestamp, and the reserved dirty marker
  start at zero. Naming the marker `dirty` does not resolve its deferred update
  rules; no access-time updates are implemented in Phase 01.
- Destruction frees lines, each set's pointer array, then the outer array.
- Use `cache_internal.h`, `lifecycle.h`, `lifecycle.c`, and a small lifecycle
  wrapper in `cache.c`. Retain the starter's separately allocated public `cache`
  interface and free it on destruction. Borrow the coherence component.

### Implementation mechanics selected by the agent

These details implement the accepted plan and delegated error handling; they
are not claimed as independently student-authored design decisions.

- Use `uint64_t` for tags and timestamps, `bool` for line flags, `size_t` for
  allocation dimensions, and unsigned integers for bit counts.
- `cache_storage_init(state, csa)` parses, validates, and allocates; on failure
  it reports to stderr, cleans partial storage, and returns false.
  `cache_storage_destroy(state)` releases owned storage and resets the object.
- Require `-s`, `-E`, `-b`; parse nonnegative decimal integers with full-string
  and range checks. E must be positive, b is in [4,10], s must permit a safe
  size_t shift, and s+b cannot exceed the 64-bit address width. Check S*E and
  aggregate host storage size before allocation. Repeated options use the last
  value. Unknown options and malformed/missing values fail initialization.
- Save parsed future settings only: default LRU, RRIP selected by `-R k`
  (k in [1,64]), and write buffering mode `-w 0|1` (default 0). Add policy,
  `rrip_bits`, and `write_buffer_mode` fields for this roadmap requirement.
  No RRIP line metadata or buffering algorithm is introduced yet.
- Accept legacy `-i` in [0,8] and `-u` in [0,10] for configuration compatibility;
  nonzero values print an explicit ignored-feature diagnostic. Neither feature
  changes storage or behavior; both remain Fall 2026 non-goals.
- Initialize pointer slots to NULL; any allocation failure uses the normal
  cleanup order, skipping absent arrays. Public-interface allocation failure
  also releases storage. Reject another init while an instance is live; allow
  init after destroy. Repeated destroy is harmless.
- Public init checks the borrowed coherence interface, finishes all allocations,
  binds entry points, resets starter transient state, then registers the callback.
  No callback is registered on initialization failure. Never free coherence.
- Preserve existing starter access/tick/callback behavior for later phases;
  it is not a cache implementation and nonempty traces are not accepted here.

## Student Proposal and Reasoning (Discussion History)

- Confirmed process decision: begin incremental implementation with `init()`
  and `destroy()` before implementing cache access behavior.
- The student recognizes that cache dimensions are runtime parameters and that
  simulator-owned representation therefore requires dynamic memory management.
- The student distinguishes the host-side metadata representation from the
  modeled cache's data-storage budget.
- Confirmed file-organization decision (2026-09-15): the student proposes a
  header under `teamCache/` for internal data-structure definitions, included
  by the C implementation. Review: this supports separating representation
  from implementation while keeping the framework's public `common/cache.h`
  interface intact. The structures' fields and ownership remain Open.
- Proposed filename: `teamCache/cache_internal.h`; use an include guard and a
  quoted local include. No header or structure definitions have been added yet.
- Proposed line representation (2026-09-15): no actual data payload; retain
  valid state, tag, last-access timestamp, and a "written/read" marker. The
  student now proposes representing set membership through nested storage
  rather than a per-line set field. Exact marker semantics remain Open.
- Confirmed storage choice: an outer `sets` array with an inner array of line
  pointers for each set. The student explicitly selects pointer elements and
  heap storage for line objects, motivated by flexible, noncontiguous placement.
  Allocation granularity is confirmed below; ownership and cleanup remain Open.
- Confirmed allocation timing and granularity: during `init()`, use the runtime
  dimensions to allocate each set's E pointer slots and separately allocate a
  heap line object for every slot. The student describes all initial line
  contents as "empty"; subsequently confirmed all four fields start at zero.
  Specification
  reminder: the number of sets is S = 2^s, not s.
- Confirmed deferral: the student requests postponing the written/read marker's
  operational semantics until eviction work. Keep this as an explicit follow-up;
  do not silently choose update rules during Phase 01.
- Confirmed initial values: valid = 0, tag = 0, last-access timestamp = 0,
  and the deferred written/read marker = 0. Student rationale: invalid lines
  cannot interfere with valid cached blocks; fields change when a block is
  installed. Review: this relies on later lookup checking validity, rather than
  treating a zero tag as an empty-line sentinel.
- Confirmed code organization: put each phase's implementation in separate C
  files or subdirectories under `teamCache/`; keep `cache.c` as the framework
  entry point calling high-level internal interfaces. Student rationale:
  readable layers and a concise central file. Exact internal interfaces and
  state ownership remain Open. Proposed Phase 01 filename: `lifecycle.c`.
- Superseded standalone-storage proposal: the student suggests a global pointer to `sets`.
  Review: this can retain the nested allocation across calls for a single active
  cache instance. A pointer alone does not retain array dimensions. Where to
  retain dimensions and which translation unit defines/exposes the pointer
  remain Open; no global state implementation has been added.
- Superseded dimension-persistence proposal: the student asked about deep-copying
  `cache_sim_args`. Review against `common/cache.h`: it contains argument count,
  argument strings, and a borrowed coherence pointer, not parsed cache
  dimensions. Copying the struct alone is shallow; duplicating argument text
  adds owned allocations and still requires parsing to recover dimensions.
  The coherence component remains framework-owned and must not be cloned or
  freed by the cache. No copy implementation was added.
- Confirmed dimension-persistence decision: retain parsed configuration values
  for later use rather than deep-copying the raw initialization arguments.
  Retained dimension fields, grouping, and global-object storage are confirmed
  below; access boundaries remain Open.
- Confirmed state grouping: retain the parsed configuration and `sets` in one
  internal structure. Student rationale: fewer separately managed pointers
  should reduce bugs. Review clarification: grouping reduces scattered state
  and can simplify passing it around, but does not reduce the pointers or heap
  allocations in the confirmed per-line allocation scheme. The structure's
  access boundaries and cleanup remain Open; global-object storage is confirmed
  below.
- Confirmed internal dimension/storage fields: `sets`, parsed `s`, `E`, `b`,
  and derived `S`, `B`. Here `S = 2^s` is the set count and `B = 2^b` is the
  block size in bytes. Review constraint: stored original and derived dimensions
  must remain consistent with each other and the allocated storage. This field
  decision does not settle future-policy state or public-interface placement.
- Confirmed internal-state storage: the student accepts a global structure
  object rather than a separately heap-allocated state object. The student
  recognizes that runtime parameters are unavailable at compile time. Their
  wording "global array" and "compiler automatically frees memory" requires
  clarification: only the fixed-size state object has static storage duration;
  the nested arrays and lines retain the confirmed heap-allocation design and
  require explicit cleanup. Normal destruction order is confirmed below.
- Confirmed normal storage destruction: free the individual lines in each set,
  then that set's line-pointer array; after all sets, free the outer `sets`
  array. Review: children are released while their containing pointer arrays
  are still accessible. Partial initialization and post-cleanup state are
  delegated below; this does not yet specify public-interface cleanup.
- Confirmed delegation: the student authorizes the agent to decide allocation
  failure corner cases without further design questions on this detail.
  Agent-selected rule within that scope: initialize pointer slots to NULL,
  check each allocation, and on failure clean up existing allocations and return
  NULL from `init()`. Cleanup guards missing outer/set arrays before traversal,
  tolerates NULL line pointers, and resets the internal state after freeing
  owned storage. Apply this when implementing lifecycle logic; not implemented
  or tested yet. This delegation does not settle unrelated design choices.
- Confirmed state placement: the student wants the actual internal state object
  defined in `cache.c`, with other implementation files knowing its type through
  the header. Review clarification: C source files are not separate processes;
  type definitions alone do not give functions access to a particular object.
  Proposed translation of this intent: a file-local static state object in
  `cache.c`, passed by pointer to internal functions when needed. The explicit
  pointer-passing interpretation is explained for student review.
- Public-interface placement remains Open: the student requests explanation
  of the existing framework `cache` type versus the internal state structure.
  No interface redesign has been authorized or implemented.
- Student requests no further discussion of parameter corner cases. Keep these
  out of the current design questions; this is not confirmation of any specific
  optional-parameter behavior, nor authorization to drop roadmap acceptance
  requirements. Required RRIP/write-buffer configuration must still be handled
  when implementing their in-scope initialization support.

## Counterexamples and Review Notes

- Deferred: after a line is written and then read, what should the written/read
  marker contain, and what does it tell a later eviction? This distinguishes
  persistent modification state from the most recent operation type.
- Resolved: `sets[s][w]` denotes a line pointer. Pointer arrays are themselves
  contiguous allocations; their target line objects need not be adjacent.
  Heap placement alone does not specify whether objects share an allocation.
- Timestamp generation/ownership and future RRIP metadata remain unresolved;
  the proposed fields do not yet constitute a complete confirmed state design.
- Resolved dimension-access question: the selected internal structure retains
  `S` and `E` alongside `sets` for later traversal. Global-object storage is now
  confirmed; access paths remain Open. Global state represents only one active instance unless
  additional instance management is designed.
- Requested design comparison: separate globals require less initial structure;
  grouping related configuration and storage in a struct makes their relationship
  explicit and permits passing the state together across internal interfaces.
  Agent preference, not student-confirmed: grouped state fits the student's
  readability goal. A globally exposed struct still permits shared mutation;
  grouping alone does not enforce encapsulation or ownership. The student has
  since selected grouped state; access boundaries remain Open.
- Requested lifetime comparison: for the current single-instance design, the
  agent favors a global internal structure object because its fixed-size fields
  do not require runtime allocation, avoiding one allocation/failure/free path.
  The nested sets and lines remain dynamically allocated. A heap state object
  offers explicit creation/destruction and can support separately owned instances,
  but one global pointer alone does not provide multi-instance support. The
  student subsequently accepted global-object storage. Public framework
  interface allocation is a separate unresolved matter.

## Proposed Design Options

The following are agent proposals and are not yet confirmed:

- Represent each line with fixed-width tag and replacement fields plus valid
  and dirty state.
- Superseded as the active storage proposal: store all `S * E` lines in one
  contiguous zero-initialized allocation. The student selected nested pointer
  arrays with separately allocated line objects.
- Store the public `cache` interface together with private configuration and
  allocation ownership in one internal simulator object.
- Use fixed-width integer types and explicit overflow checks for shifts and
  allocation-size multiplication.
- Parse future-phase options during initialization but leave their operational
  behavior disabled until the corresponding phase.

## Open Design Questions

The following list is historical. Phase 01 choices are resolved by the current
contract above; access algorithms, marker updates, and RRIP behavior remain
deferred to their phases. Tests below operationalize the already documented
Phase 01 acceptance requirements, not future cache algorithms.

- Remaining pre-implementation decisions, reviewed with the student: cross-file
  state access, public-interface placement/lifetime, and parameter behavior
  (missing dimensions and optional/future-phase settings). Concrete acceptance
  inputs are still needed before acceptance. C syntax, build wiring, and the
  delegated allocation-failure mechanics do not need additional student design.
- Should the public interface be embedded in the internal simulator object, or
  should interface and internal state use separate allocations?
- Which state owns the confirmed nested pointer arrays and individual lines?
- Dimension persistence is confirmed as `s`, `E`, `b`, `S`, `B`; any additional
  policy configuration remains to be specified. Address masks belong to later
  address-decoding discussion unless explicitly needed for Phase 01.
- Given the required `NULL` failure return, what diagnostics should `init()`
  provide? How should failure acceptance be checked
  given the engine's missing `NULL` handling?
- Which non-required options should be accepted and ignored, rejected, or
  explicitly deferred?
  Inspection of `ex_rrip.config` and `ex_wb.config` shows both supply `-i 4`,
  although victim cache is not required for Fall 2026. The first also supplies
  `-R 3`, the second `-w 1`. Their initialization compatibility therefore depends
  on the option-handling decision; silently assuming these examples omit
  non-required features would be incorrect.

## Lifecycle Invariants (Verified)

- Every successfully initialized line begins invalid and clean with reset
  replacement metadata.
- Every successful allocation has exactly one simulator owner and one cleanup
  path.
- The cache borrows the coherence pointer and never frees the coherence
  component.
- Derived dimensions and masks cannot result from undefined shifts or wrapped
  allocation arithmetic.
- `destroy()` leaves no simulator-owned allocation reachable and can safely
  clear module-level state.

## Confirmed Pseudocode

Partial student-confirmed behavior: initialization creates nested set/way
pointer arrays and allocates every line individually on the heap; all lines
start with valid, tag, last-access timestamp, and the written/read marker all
zero.

Student-confirmed normal storage cleanup:

```text
For each of the S sets:
    Free each of its E individually allocated line objects.
    Free this set's line-pointer array.
Free the outer sets array.
```

Agent-selected extension under explicit student delegation: start pointer slots
at NULL; on any allocation failure, use the same cleanup order while skipping
missing arrays and allowing NULL line pointers. Reset internal state after
cleanup and return NULL on initialization failure.

Completion of the accepted lifecycle plan, including delegated mechanics:

```text
init(csa):
    Reject invalid framework arguments or an already-live instance.
    Parse and validate configuration into the file-local state.
    Compute S, B, line count and allocation sizes without overflow.
    Allocate the outer S-slot pointer array with NULL slots.
    For each set, allocate E NULL pointer slots.
        For each way, allocate a line and initialize its fields to zero.
    Allocate the public cache interface and bind its function pointers.
    Borrow coherence, reset starter transient state, register the callback.
    Return the public interface.
    On any failure: clean owned allocations, reset state, return NULL.

destroy():
    Release storage in the student's confirmed child-before-parent order.
    Reset internal state; free and clear the public interface pointer.
    Clear borrowed/transient pointers without freeing framework components.
    Return success.
```

## Acceptance Behaviors and Test Scenarios

Use the Phase 01 acceptance behaviors in `phase-roadmap.md`, under the student's
request to execute this phase. Agent-selected routine checks of those behaviors:

- Small configurations (including s=0, E=1 and b endpoints) have exact S/B and
  all-zero lines; defaults and parsed -R/-w values persist independently.
- Invalid dimensions/overflow fail before allocating or registering callbacks.
- Inject failure at every allocation of a small nested cache and its public
  interface; assert no owned allocations remain and retry can succeed.
- Public-interface initialization, idle tick, destruction, and reinitialization
  use a borrowed fake coherence component without freeing it.
- Run an empty trace through the actual engine in separate processes using
  different configurations. Check component-owned memory with leak tooling.
- No access/replacement test cases are introduced; those remain student-directed
  work for later phases.

## Implementation Mapping

The student-confirmed flat source layout is implemented. Test scaffolding is
kept in a separate `tests/` directory and is not compiled into the component.

| File / symbol | Responsibility |
| --- | --- |
| `teamCache/cache_internal.h` | `cache_line`, `cache_policy`, `cache_state` definitions |
| `teamCache/lifecycle.h` | `cache_storage_init` / `cache_storage_destroy` contracts |
| `teamCache/lifecycle.c:parse_options` | Checked decimal parsing, defaults, future settings, compatibility diagnostics |
| `teamCache/lifecycle.c:derive_dimensions` | Safe S/B/line-count and allocation-size calculations |
| `teamCache/lifecycle.c:cache_storage_init` | Individual allocations, zero initialization, failure cleanup |
| `teamCache/lifecycle.c:cache_storage_destroy` | Child-before-parent cleanup and state reset |
| `teamCache/cache.c:init` | Private global state, public interface, borrowed coherence and callback registration |
| `teamCache/cache.c:destroy` | Storage delegation, public-interface free, transient-state reset |
| `teamCache/CMakeLists.txt` | Compile `cache.c` and `lifecycle.c` into `teamCache` |
| `tests/teamCache/phase01_lifecycle_test.c` | Lifecycle acceptance harness and allocation-failure injection |

Existing starter access/tick/callback functions remain in `cache.c`; later
phases will replace their placeholder behavior in the planned separate modules.

## Commands and Observed Results

### Implementation validation: 2026-09-15

**Passing checks**

- `teamCache` builds and produces
  `/tmp/cadss-phase01-build/teamCache/libteamCache.so`.
- Lifecycle harness passes: dimensions, zero line metadata, independent line
  objects, LRU/no-buffer defaults, future option parsing, and reset between
  configurations. Fifteen invalid argument configurations fail before allocating
  storage or registering a callback.
- All 10 allocations in a two-set, three-way public initialization were failed
  individually, including the public interface allocation. Every failure leaves
  zero owned allocations, does not register a callback, and permits a successful
  retry. Repeated destroy and rejection of a second live init also pass.
- Independent harness Valgrind: **190 allocations, 190 frees; 0 bytes in 0
  blocks at exit; 0 errors**. It verifies that borrowed coherence is not freed
  or destroyed and an idle tick reaches coherence exactly once.
- Four separate real-engine processes using `-c teamCache` and a zero-byte
  trace exit 0 with `Ticks - 1`: `(s,E,b)=(0,1,4)`, `(2,3,10)`, `(1,2,5)`
  with `-R 3`, and `(1,2,5)` with `-w 1`. The framework prints a trailing NUL
  after its tick summary; this is unchanged framework behavior.
- `git diff --check` passes. Strict C11 compilation succeeds; it reports only
  the starter's unused `procNum`, `addr`, and `outFd` parameters. The new lifecycle
  source and test harness add no compiler warnings.

**Environment/framework limitations**

- Full `cmake --build /tmp/cadss-phase01-build -j 4` fails in
  `trace/taskLib/ct_file.h` because `zlib.h` is missing. Building the targets
  needed for plain-text traces succeeds without changing the framework.
- The full engine under Valgrind reports **240 definitely lost bytes in 9
  blocks**, plus 4,491 reachable bytes. Allocation stacks identify the existing
  processor, trace, branch, engine/configuration, coherence/interconnect/memory,
  and loader code; none identifies `teamCache` or `cache_storage_init`.
  The whole engine is therefore **not leak-free**. The isolated component
  result above is the evidence for Phase 01's owned-allocation requirement.
- Invalid-input acceptance calls `init()` directly: engine's existing empty
  NULL-return branch still prevents clean end-to-end rejection of invalid
  configurations. The component returns NULL as specified; engine is unchanged.
- A clean full build on the recommended course machines has not been run.

### Reproduction commands

Run from the repository root:

```sh
cmake -S . -B /tmp/cadss-phase01-build -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build /tmp/cadss-phase01-build --target teamCache cadss-engine trace processor branch coherence interconnect memory -j 4
gcc -std=c11 -g -O0 -Wall -Wextra -Wpedantic -Icommon -IteamCache tests/teamCache/phase01_lifecycle_test.c teamCache/cache.c teamCache/lifecycle.c -Wl,--wrap=calloc -Wl,--wrap=free -o /tmp/cadss-phase01-build/phase01_lifecycle_test
/tmp/cadss-phase01-build/phase01_lifecycle_test
valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=99 --log-file=/tmp/cadss-phase01-build/lifecycle-valgrind.log /tmp/cadss-phase01-build/phase01_lifecycle_test
git diff --check
```

The root CMake file puts `cadss-engine` in the source root even for an out-of-tree
build. Libraries go in the build tree; use that tree as the engine's working
directory. The following recreates the four temporary configurations and trace:

```sh
python3 - <<'PY'
from pathlib import Path
import subprocess

root = Path.cwd()
build = Path('/tmp/cadss-phase01-build')
trace = build / 'phase01-empty.trace'
trace.write_text('')
cases = [
    ('minimum', '-s 0 -E 1 -b 4'),
    ('large_block', '-s 2 -E 3 -b 10'),
    ('rrip', '-s 1 -E 2 -b 5 -R 3'),
    ('write_buffer', '-s 1 -E 2 -b 5 -w 1'),
]
for name, options in cases:
    config = build / f'phase01-{name}.config'
    config.write_text(f'__processor\n__cache {options}\n__branch\n'
                      '__coherence\n__interconnect\n__memory\n')
    result = subprocess.run([str(root / 'cadss-engine'), '-c', 'teamCache',
                             '-s', str(config), '-t', str(trace)],
                            cwd=build, capture_output=True, text=True, timeout=15)
    print(name, result.returncode, repr(result.stdout), repr(result.stderr))
    assert result.returncode == 0 and 'Ticks - 1' in result.stdout
PY
```

Full-engine leak inspection (reports existing framework leaks):

```sh
cd /tmp/cadss-phase01-build
valgrind --leak-check=full --show-leak-kinds=all --keep-debuginfo=yes --log-file=/tmp/cadss-phase01-build/engine-valgrind.log /home/chenyy/cadss_public/cadss-engine -c teamCache -s /tmp/cadss-phase01-build/phase01-minimum.config -t /tmp/cadss-phase01-build/phase01-empty.trace
```

### Progress review: 2026-09-15

- Read the roadmap, Phase 00/01 records, README, public cache/coherence/common
  headers, `teamCache/cache.c`, engine initialization and shutdown, processor
  destruction, and the prior Cache Lab's representation/allocation code.
- Extracted the local assignment PDF using Python 3 `pypdf`; `pdftotext` was
  unavailable. Sections 3/4 specify `b` in [4, 10], default LRU, default write
  buffering disabled, and `NULL` on initialization failure. These are external
  constraints, not newly student-confirmed design choices.
- Source inspection confirms parameter switch branches remain empty, no line
  storage exists, and `destroy()` performs no cleanup. Interface function
  assignment and coherence callback registration already exist in the starter.
- `engine/engine.c` has an empty branch for a `NULL` cache initialization result
  and later dereferences the pointer. This framework limitation must be
  distinguished from the cache's required failure return during acceptance;
  no framework change or failure-test approach has been selected.
- Shutdown reaches the cache through `processor/processor.c:destroy()`, which
  calls `cs->si.destroy()`.
- No implementation changes or build/runtime checks were performed during this
  review. Design remains Proposed; pseudocode and concrete acceptance scenarios
  remain Open. Next discussion: the student's line/state representation and
  allocation ownership proposal.

## Deviations, Follow-Ups, and Superseded Decisions

- Deferred by student: resolve written/read marker semantics for eviction in
  Phase 04, or revisit explicitly if Phase 03 store behavior requires that
  decision earlier. The initial clean-state requirement remains in Phase 01;
  the marker's initial value is confirmed as zero, while its update semantics
  remain deferred.
