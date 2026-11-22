# src/job

The job system (SPEC §13, D-29). **Not implemented yet**: task 1B-12 (`job.{h,c}`). Nothing here is compiled today. Simulation, rendering and the engine modules run on the main thread (scratch arenas, profiler zones, draw2d say "main thread" in their contracts until this lands); only the protocol's stdin reader thread and `bmf_log` (any thread, spin lock) run off it.

## Invariants (to keep)
- One API on every platform (D-29). On web in Phase 1 (no threads) and with `--jobs 0`, jobs run inline on the calling thread; because of SPEC §6 rule 6 the results are identical, so replays and golden hashes do not depend on the worker count.
- Determinism: jobs write their outputs by index into disjoint ranges (no shared accumulators, no order-dependent reductions). Parallel-for splits index ranges into fixed-size chunks, never into "one chunk per worker".
- Every worker sets MXCSR like the main thread (D-37 rule 10: no FTZ/DAZ, round to nearest) and gets its own scratch arena pair.
- Waiting participates: the thread that waits runs queued jobs itself.

## Data layout (planned)
Worker pool of `min(cores - 1, 7)` threads; a bounded MPMC ring queue (own implementation of the Vyukov-style algorithm from its published description); fixed-capacity job records, no heap (D-16).

## Hot paths (planned)
Physics narrowphase over the pair array and solver islands, draw2d sorting, procedural generation (SPEC §13). Main thread: platform events, simulation, render submit; the audio thread runs the mixer.

## Limits (planned)
At most 7 workers. Jobs must not call main-thread-only functions (platform window, GPU, protocol, profiler zones).
