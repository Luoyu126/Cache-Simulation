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
