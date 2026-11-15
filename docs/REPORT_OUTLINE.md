# Final report outline

Follows the synopsis (problem, methodology, outcomes). Adjust headings and length to your department's
format. Each chapter says what to write and which file in this repository to draw from.

## Front matter
Title, your name, guide, department, college, year. Abstract (half a page): the problem, what you built,
the main measured result, the main lesson.

## 1. Introduction
* Why electronic markets need order books and matching engines; why latency matters.
* **Problem statement:** use the synopsis wording (existing student projects ignore performance, use unsuitable data structures, do not measure).
* **Objectives:** the five objectives of the synopsis, one line each.
* Scope: what is in (limit/market orders, add/cancel/modify, price-time priority, CSV I/O, benchmarks) and what is not.

## 2. Background (literature study)
* Order book, bid/ask/spread, order types, price-time priority (use the worked example from `docs/DESIGN.md`).
* Basic ideas of low-latency software: cache effects, allocation cost, tail latency, why single-threaded matching.
* Related work: summarize the implementations, articles and talks **you actually read**, and cite them. Do not
  cite anything you have not read.

## 3. System design
* Requirements and module diagram (synopsis modules mapped to code: `DESIGN.md`, section 2).
* Data structures and why (section 3), complexity table (section 4), matching/cancel/modify algorithms (section 5),
  invariants (section 6).
* File formats: input and output CSV.
* **Figures to draw:** (1) architecture / data flow, (2) an order book with price levels and FIFO queues,
  (3) flowchart of matching.

## 4. Implementation
* Tools: C++17, CMake, compiler and version, Git/GitHub, CI.
* Code organization (project layout in the README).
* Short excerpts only (10-20 lines each): `OrderBook::match`, `OrderBook::cancel`, `parseCommand`.
* C++ features used and why: `std::map` with custom comparator, iterators that stay valid, templates,
  `std::from_chars`, allocators, `std::optional`.
* Problems met and how they were solved (examples: forgetting to erase from the index before `pop_front`;
  the wrap-around case in hash-table deletion; the `NDEBUG`/`assert` trap).

## 5. Testing
* Strategy: hand-written cases, differential test against a reference model, stress tests, mutation checks,
  sanitizers.
* **Table:** test suite -> what it covers -> number of checks (run `ctest` and copy the numbers).
* Result of the mutation checks (how many deliberate bugs were caught; the one gap that was found and fixed).

## 6. Evaluation
* **Setup (record all of this):** CPU model, cores, RAM, OS, compiler and version, build type and flags,
  number of runs, date. Close other programs and plug in the laptop.
* Workload: the synthetic generator (mix of orders, seed).
* Method: throughput vs latency timing, warm-up, percentiles, interleaved comparison (`scripts/compare.py`).
* **Results tables:** version | M commands/s | p50 | p99 | p99.9 (from `docs/optimization_log.md`, with **your** numbers).
* **Figures:** bar chart of throughput per version; latency percentiles per command type.
* Cost of a feature: timestamps cost about 3% / a few ns; say how you measured it, and the rejected packing experiment.
* Discussion: what helped (pooling), what did not (flat hash: marginal; identity hash: rejected, and why),
  what you could not explain, and what the limits of the measurement are.

## 7. Conclusion and future scope
* What was achieved against each objective (a short table is good).
* Limitations (README) and honest deviations from the synopsis (`DESIGN.md`, section 8).
* Future work: flat array of price levels, intrusive list, pre-sized index, timestamps and snapshot log,
  more order types, multithreaded pipeline, real market data replay.

## References
Only sources you read and used.

## Appendices
A. Build and run instructions. B. Sample input and output files. C. Full test list. D. Repository link and commit hash of the submitted version.
