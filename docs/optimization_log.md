# NanoBook optimization log (Step 6)

## Method

Every change follows the same loop:

1. **Hypothesis**: what should get faster, and why.
2. **Change one thing.**
3. **Run all tests**: the unit tests, the random reference-model test and the hash-table
   stress tests are the safety net. Outputs must stay identical (we also compared `trades.csv`
   byte-for-byte against the baseline on a 1,000,000-command workload).
4. **Measure** with `scripts/compare.py`: the versions are run *interleaved* (A, B, A, B, ...)
   over several rounds, and the median is reported with the min-max spread. Differences smaller
   than the spread are not evidence.
5. **Keep or revert** based on numbers, not on belief.

Workload: `nanobook_bench` synthetic orders, seed 42 (~70% limit, 5% market, 20% cancel, 5% modify).
Latencies are in nanoseconds and include ~30 ns of clock-reading overhead.

> **The numbers below were measured in a shared 1-vCPU cloud sandbox. Re-run
> `scripts/compare.py` on your own machine and replace them with your own results.**

## Versions

| Version | What changed |
|---|---|
| v0 baseline | `std::map` of price levels, `std::list` per level, `std::unordered_map` for the order-ID index; all default allocators |
| v1 pool | node-based containers use `PoolAllocator` (free-list of fixed-size slots) instead of one `malloc` per node |
| v2 pool + flat | order-ID index is a `FlatIdMap` (one contiguous open-addressing table) instead of `std::unordered_map` |
| v3 timestamps | `Order`, `Command` and `Trade` carry a `Timestamp` (Step 8, a feature, not an optimization: its cost was measured) |

## Results

1,000,000 commands, 7 interleaved rounds:

| Version | M cmds/s (median) | min-max | p50 ns | p99 ns | p99.9 ns |
|---|---|---|---|---|---|
| v0 baseline | 7.11 | 6.76-7.36 | 143 | 728 | 1672 |
| v1 pool | 8.39 | 7.73-8.83 | 125 | 491 | 888 |
| v2 pool + flat | 8.77 | 7.38-9.24 | 130 | 477 | 889 |

Extra comparisons of v1 vs v2:

| Workload | v1 M cmds/s | v2 M cmds/s | v1 p50 / p99 | v2 p50 / p99 |
|---|---|---|---|---|
| 3,000,000 commands, 5 rounds | 6.58 | 6.85 | 152 / 790 | 163 / 615 |
| 2,000,000 commands, 5 rounds | 7.40 | 7.47 | 138 / 645 | 149 / 560 |

An earlier 7-round run of v0 vs v1 alone gave 7.18 -> 8.16 M cmds/s (1.14x).

## Final consolidated run (all versions, one session)

1,000,000 commands, 6 interleaved rounds:

| Version | M cmds/s (median) | min-max | p50 ns | p99 ns | p99.9 ns |
|---|---|---|---|---|---|
| v0 baseline | 7.39 | 6.41-7.83 | 135 | 507 | 856 |
| v1 pool | 8.88 | 8.65-9.23 | 125 | 443 | 734 |
| v2 pool + flat | 9.41 | 8.88-9.60 | 114 | 400 | 617 |
| v3 timestamps | 9.32 | 8.57-9.86 | 116 | 416 | 643 |

(Absolute speed differs between sessions because the shared machine drifts; compare versions only within one table.)

## Cost of a feature: timestamps (Step 8)

Adding a `Timestamp` to `Order` and `Trade` grows both from 32 to 40 bytes. Measured against v2 in two separate
interleaved runs: throughput -3% and +1% (inside the noise), p50 +5 to +7 ns, p99 +7 to +10%. Small, but not zero,
and it was worth measuring instead of assuming.

**Rejected experiment: packing.** Hypothesis: the cost comes from the bigger struct, so reordering the members
(biggest first) and giving the enums a 1-byte type should bring `Order` back to 32 bytes and recover the speed.
First attempt: `sizeof(Order)` was still 40, because an `enum class` is 4 bytes by default. With `std::uint8_t`
enums it became 32. Result: no measurable gain (9.02 vs 9.11 vs 8.85 M cmds/s for v2, v3, packed, all overlapping),
and it needs a constructor to keep the call syntax. Not adopted. Lesson: a hypothesis can be reasonable and still wrong.

## What we learned

* **v1 (pooling): a real, repeatable gain**, about 1.14-1.18x throughput, and p99 fell from ~700 to ~500 ns.
  Hypothesis confirmed: removing a `malloc`/`free` per order (and keeping nodes close together) helps.
* **v2 (flat hash): small and mixed.** Throughput +1% to +5%, which is inside the noise; p99 improved
  in every comparison (about 3-22%), p50 got slightly worse (about 4-8%). Kept mainly for the tail,
  at the cost of more code and one rule (order id 0 is reserved).
* **Experiment that was rejected:** an identity hash (`key & mask`) for the flat table, hoping that
  sequential order ids stay adjacent in memory. Median latency did improve (117 vs 149 ns), but
  throughput fell to 0.71x and p99 rose to ~1750 ns. Reason: sequential ids fill the table with long
  contiguous runs ("primary clustering"), and every erase must scan to the end of its run.
  The multiplicative (Fibonacci) hash spreads keys and avoids this.
* **The profiler was not trustworthy here.** `gprof` in the sandbox attributed time to functions that are
  never called in the benchmark, so no decision was based on it. On your own machine use `perf`
  (Linux) or the Visual Studio profiler, and check that it agrees with the experiments.
* **Mutation checks found a real hole in the tests.** Deliberately breaking the hash table's
  erase() showed that the wrap-around branch (clusters crossing the end of the array) was untested.
  A test with tiny tables and many random keys now covers it.

## Known limits of these changes

* `PoolAllocator` is not thread-safe and never returns memory to the OS.
* `FlatIdMap` rehashes (doubles) inside `insert()`, which can cause an occasional latency spike;
  reserving capacity up front would avoid it.
* Pooling and the flat table do not change the algorithms: price levels are still a `std::map`
  (O(log P) per new level and per cancel's level lookup).

## Ideas not done yet (each would get its own hypothesis / measurement)

1. Store the price-level iterator in each order's index entry, so cancel needs no map lookup.
2. Replace the `std::map` of price levels by a flat array indexed by `price - base` (assumes a bounded price range).
3. Replace `std::list` by an intrusive linked list in one pooled array (fewer pointer hops).
4. Pre-size the order index (`reserve`) to remove rehash spikes.
5. Compiler flags: compare `-O2` vs `-O3`, and link-time optimization (`-flto`); measure, do not assume.
