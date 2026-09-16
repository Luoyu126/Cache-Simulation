# Phase 07: Request Queueing

## Status

- Design: Confirmed
- Implementation: Implemented
- Acceptance: Accepted locally

## Scope and Non-Goals

Phase 07 allows a new original processor request to be accepted and retained
while another original request is active. Requests must retain their own
operation, processor, request tag, callback, and split progress, execute in the
confirmed order, and complete exactly once.

Write buffering remains Phase 08. Phase 07 does not change lookup,
replacement, coherence, eviction, or the already-confirmed serial ordering of
the block parts belonging to one original request.

## Relevant Specification and Code

- `docs/Design/phase-roadmap.md`: Phase 07 scope and acceptance behavior.
- `docs/346f26 P1-cache.pdf`, page 2: a second processor request must be
  queued while the cache has one outstanding request.
- `teamCache/access.c:cache_access_request`: currently terminates when
  `active`, `completion`, or the existing block queue is occupied.
- `teamCache/split.h/.c`: owns one completion-count record and a FIFO containing
  the cache-line parts of one original processor request.
- `teamCache/cache_internal.h`: has one `active` block, one block-part FIFO,
  and one `completion` record.

## Current Implementation Assessment

The existing Phase 05 queue is not the Phase 07 request queue:

- Its entries are the cache-line parts of one original processor request.
- `cache_split_prepare` requires all active, queue, and completion state to be
  empty before constructing those parts.
- `cache_completion` is explicitly one keyed entry for one original request.
- A second original request is rejected with
  `overlapping requests require Phase 07 queueing`.

Therefore Phase 05 provided reusable serial block-processing mechanics, while
Phase 07 adds the separate original-request FIFO described below.

## Resolved Design Questions

- Original requests use an outer arrival-order FIFO; current-request block
  parts continue to use the existing inner FIFO.
- Each outer node owns a value copy of the request and completion identity.
- A completed request starts the next outer FIFO entry in the same tick, but
  that newly started request cannot retire until a later tick.
- Destruction owns and releases every remaining outer and inner queue entry.

## Student Proposal and Review

Student proposal (`Confirmed`):

- Add an outer FIFO queue of original processor requests in arrival order.
- Each arriving call contributes exactly one original request to that queue.
- Take one original request from the outer queue when it is ready to execute.
- Only then determine its covered cache blocks and use the existing Phase 05
  inner block queue.
- Process the inner queue serially from the lowest block address before moving
  to the next original request.
- When original request A completes on tick T, invoke A's callback and start
  the next queued original request B in the same tick.
- If B becomes ready immediately, including by hitting, do not retire or
  callback B until a later tick.

Review results:

- This cleanly separates between-request queueing from Phase 05's
  within-request block queue.
- The outer queue entry must own a value copy of `trace_op`, processor number,
  request tag, and callback. It cannot retain the incoming `trace_op*`, because
  the processor frees that object after `memoryRequest` returns.
- Deferring splitting until an original request reaches the front avoids
  interleaving block parts from different requests and allows the existing
  one-entry `cache_completion` aggregation to remain scoped to only the
  current original request.
- Starting B in A's completion tick preserves the student's Phase 05
  no-idle-transition rule while the later retirement boundary preserves the
  required nonsynchronous callback behavior.

## Confirmed Pseudocode

```text
memoryRequest(op, processor, tag, callback):
    copy one complete original request into the outer FIFO
    if no original request is currently executing:
        take the FIFO head
        split it into the existing low-address-first inner block FIFO
        start its first block

tick:
    progress the current original request using the Phase 05 block rules
    when its final block retires:
        invoke that original request's callback once
        if the outer FIFO is not empty:
            take its head, split it, and start its first block in this tick
            leave any newly ready block for a later tick

destroy:
    release the active and inner-queued block state
    release every original request still in the outer FIFO
```

## Dependencies

- Phase 05 serializes all block parts of one original request and aggregates
  them into one callback.
- Phase 06 changes only replacement behavior and does not alter request
  ownership.

## Implementation Mapping

- `teamCache/request_queue.h/.c`: owned outer FIFO entries, enqueue/dequeue,
  and destruction.
- `teamCache/cache_internal.h`: outer FIFO head and tail.
- `teamCache/access.c`: enqueue every arriving original request, start an idle
  head, and start the next queued request in the completing request's tick.
- `teamCache/split.c`: unchanged inner FIFO; splits only the current outer
  request after it reaches the head.
- `teamCache/CMakeLists.txt`: compiles `request_queue.c`.
- `teamCache/tests/phase07_queue_test.c`: FIFO identity, caller-storage copy,
  same-tick successor start, later callback boundary, queued split ordering,
  and destruction of both queue levels.

## Commands and Observed Results

```sh
cmake -S . -B /tmp/cadss-phase07-build -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build /tmp/cadss-phase07-build --target teamCache cadss-engine trace \
  processor branch coherence interconnect memory -j 4

gcc -std=c11 -g -O0 -Wall -Wextra -Wpedantic -Werror \
  -Icommon -IteamCache teamCache/tests/phase07_queue_test.c \
  teamCache/cache.c teamCache/access.c teamCache/eviction.c \
  teamCache/replacement.c teamCache/request_queue.c teamCache/split.c \
  teamCache/lookup.c teamCache/lifecycle.c -o /tmp/phase07_queue_test
/tmp/phase07_queue_test

valgrind --leak-check=full --show-leak-kinds=all \
  --errors-for-leak-kinds=all --error-exitcode=99 /tmp/phase07_queue_test
git diff --check
```

- The component and required framework targets build successfully.
- The Phase 07 harness passes. Three overlapping original requests retain FIFO
  order and distinct processor/tag identities; the third request also retains
  its original address after caller storage is changed and later splits into
  low/high blocks.
- A successor starts in the completing request's tick but cannot callback
  until a later tick.
- Destroying during an active miss frees the active block, inner split state,
  and all queued original requests without callbacks.
- Phase 01--06 regression harnesses pass.
- Valgrind reports 28 allocations, 28 frees, zero bytes at exit, and zero
  errors.
- `git diff --check` passes.
