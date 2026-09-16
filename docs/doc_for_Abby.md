# Cache receives a hit request in iteration 102, but reference completes it in iteration 104

Hi Professor Railing,

I'm confused about the expected cache-hit timing. The handout says:

> If a request is a hit or complete, the processor should be notified via the callback on the next tick.

With `-s 1 -E 2 -b 4 -i 0 -u 0 -w 0`, I tested:

**Baseline:**

```text
L 10,1
```

**One miss followed by one hit:**

```text
L 10,1
L 10,1
```

| Trace | My implementation | Reference |
|---|---:|---:|
| Baseline | 102 ticks | 102 ticks |
| Baseline + one hit | 103 ticks | 104 ticks |

**The key detail is that the cache actually receives the second request in iteration 102, not 103.** I checked `processor.c`: it first calls the cache's `tick()`. When the previous miss completes, its callback clears `pendingMem`. The processor then continues within that same iteration, fetches the next instruction, and directly calls `cache->memoryRequest()`.

I verified this with a GDB breakpoint **inside my cache's `memoryRequest()`**, rather than only at the trace-reading site:

```text
Iteration   1: cache receives the first load (miss)
Iteration 102: first load completes
Iteration 102: cache receives the second load (hit)
Iteration 103: my cache completes the second load
```

My implementation marks the hit `READY` when it receives the request and invokes the processor callback on the next cache tick. However, the reference takes 104 ticks for this testcase. In a separate GDB run with repeated hits, I also confirmed that the reference receives a hit request in iteration 102 and completes it in iteration 104.

Since the request has already reached the cache in iteration 102, shouldn't "on the next tick" mean iteration 103? Is an additional hit-processing cycle required before the request becomes ready for notification?

Thank you!

---

# Thursday follow-up: split-line eviction difference

## Important context

This is **not an instructor-provided trace**. It is a boundary trace we created
to compare `teamCache` with `refCache`. The instructor-provided traces checked
so far (`load.trace`, `wide.trace`, and `wb-test.trace`) match `refCache` in
verbose classifications and total ticks.

Please keep this edge case in mind during Phase 09, and ask Professor Railing
about it in Thursday's class before changing the implementation solely to
match the reference binary.

## Minimal reproduction

Cache configuration:

```text
__processor
__cache -s 0 -E 1 -b 4 -i 0 -u 0 -w 0
__branch
__coherence
__interconnect
__memory
```

Trace:

```text
L 0,1
L f,2
```

This configuration has one set, one way, and 16-byte blocks. The first load
places block `0x0` in the only cache line. The second load covers bytes
`0xf..0x10`, so it must be split into:

1. address `0xf` in block `0x0`, which is a hit;
2. address `0x10` in block `0x10`, which has a different tag and maps to the
   same full set.

## Observed output

`teamCache`:

```text
Ticks - 305
[0] Address: (nil) is a Miss
[0] Address: (nil) is a Hit
  [0] Address: 0x10 is also a Evict
```

`refCache`:

```text
Ticks - 204
[0] Address: (nil) is a Miss
[0] Address: (nil) is a Hit
  [0] Address: 0x10 is also a Hit
```

The 101-tick difference comes from `teamCache` performing the second block's
eviction and memory-fetch path. `refCache` treats that block as a hit.

## Follow-up evidence

Appending a third operation makes the state difference observable:

```text
L 0,1
L f,2
L 0,1
```

`teamCache` reports the final `L 0,1` as an eviction and takes 507 ticks,
showing that block `0x10` replaced block `0x0`. `refCache` reports the final
load as a hit and takes 206 ticks, showing that its split access did not replace
block `0x0`.

## Question for Professor Railing

The handout says a cross-line request accesses the lower-address block first
and that each block should receive its own cache processing. In this
direct-mapped example, should block `0x10` independently miss and replace block
`0x0`, as `teamCache` does, or should we reproduce the supplied `refCache`
behavior and treat the second block as a hit?

Until this is clarified, the implementation keeps the independent-block
behavior because changing it to match this reference result would conflict
with the handout's split-access requirement.
