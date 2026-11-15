# Viva preparation

Use this as a checklist, not a script. The goal is that you can explain each answer in your own words
and reproduce it on a whiteboard. If you cannot, re-read the matching code until you can.

## 1. The 60-second pitch

"NanoBook is a limit order book and matching engine written in C++17. An exchange keeps buy and sell orders
in an order book and matches them automatically: best price first, and at the same price the earliest order
first. My engine supports limit and market orders, cancel and modify, reads orders from a CSV file and writes
the trades to another. I tested it against a simple reference model with random input, and I measured it:
about **[your number]** million commands per second, with a median latency of about **[your number]** ns.
I improved it step by step, keeping only the changes that the measurements justified."

Fill in the two numbers from **your own** machine (`./build/nanobook_bench 1000000`).

## 2. How the project was built (tell it in this order)

1. Core slice: `Order`, one `OrderBook`, price-time matching, hard-coded demo.
2. Cancel, modify and market orders; the order-id index that makes cancel fast.
3. CSV input and output, CMake, splitting into headers and sources.
4. Tests: hand-written cases, a random test against a reference model, mutation checks.
5. Benchmark: synthetic workload, throughput and latency percentiles.
6. Optimization: pooling allocator and flat hash table, measured with interleaved A/B runs.
7. Documentation, GitHub, report outline.
8. Timestamps and the best bid/ask snapshot log (the two gaps between the synopsis and the first version).

If your department has rules about AI assistance, follow them and be upfront about how you used it. Either
way, be ready to change the code live (for example, print the spread, or change a rule) and to explain
every line you are asked about.

## 3. Likely questions, with the idea of a good answer

**What is an order book / matching engine?**
A list of waiting buy orders (bids) and sell orders (asks), and the logic that trades them when prices
cross. The best bid is the highest, the best ask the lowest.

**What is price-time priority?**
Better price wins; among equal prices, earlier arrival wins. Example: two sells at 101, #1 then #2; a buy for
12 trades 10 with #1 first, then 2 with #2.

**Limit vs market order?**
Limit: trade at this price or better, the rest waits in the book. Market: trade now at any price; whatever
cannot trade is discarded, it never rests.

**Why integer prices?**
Floating-point numbers cannot be compared exactly (0.1 + 0.2 != 0.3), and matching is built on comparisons.
Integers (ticks) are exact and faster.

**Why `std::map` of `std::list`s plus a hash map? Why not one sorted vector?**
Map: levels stay sorted and the best price is `begin()`. List per level: FIFO, and erasing from the middle is O(1)
with an iterator that stays valid. Hash map: finds an order by id without searching. A sorted vector would make
insertion of a new price level O(n).

**How is cancel fast? What does it cost?**
Hash lookup (O(1) expected) gives the stored iterator; erasing from the list is O(1); finding the level to
delete it when empty is O(log P). Without the id index it would be a scan of all levels.

**What happens when you modify an order?**
Same price and a smaller or equal quantity: edited in place and keeps its queue position. Anything else: cancel
and re-add, so it goes to the back of the queue and may trade at once if the new price crosses the book.

**At what price does a trade happen?**
At the resting order's price. The newcomer may get a better price than it asked for ("price improvement").

**How do you know it is correct?**
Hand-written cases for each rule; 20,000 random commands compared step by step with a deliberately simple
reference model; a check that the book is never crossed; and mutation checks where I broke the code on purpose
to prove that the tests fail.

**Why not `assert`?**
Release builds define `NDEBUG`, which removes `assert`, so the tests would silently check nothing.

**How did you measure performance, and why is it trustworthy?**
Throughput: the whole loop is timed once, after a warm-up run, so clock calls do not distort it. Latency: each
command is timed individually, and the clock overhead (about 30 ns) is reported. A fixed seed gives identical
workloads. Versions are compared interleaved (A, B, A, B), with median and min-max, so machine drift is not
mistaken for improvement.

**Why look at p99 and not just the average?**
A trading system is judged by its slow cases; the average hides them. p99 means 1% of commands were slower than
this value.

**What optimizations did you do, and what were the results?**
Pooling allocator: a real gain (about 1.14 to 1.18x), because it removes a `malloc` per order. Flat hash table:
marginal (+1 to 5% throughput, inside the noise), better p99, slightly worse p50. An identity-hash variant was
tried and rejected: it made throughput worse (primary clustering with sequential ids).

**How does the pool allocator work?**
It carves big chunks into fixed-size slots linked in a free list. Allocate pops one, free pushes it back, and
the most recently freed slot is reused first, so it is probably still in cache. Not thread-safe; never returns
memory to the OS.

**How does the flat hash table delete an entry?**
Linear probing walks until an empty slot, so just emptying a slot would hide later keys. Backward-shift deletion
moves later entries of the same cluster into the hole whenever their home slot allows it.

**What is the timestamp for, and why does it not decide priority?**
It records when an order arrived and when a trade happened, for logs and analysis. Priority is the position in the
FIFO queue instead, because timestamps can tie or be missing, and queue order is exact. A trade's timestamp is the
arrival time of the incoming order, since that is when the trade occurred. (Test: the earlier-arriving order with the
later timestamp still trades first.)

**What does the snapshot log contain, and why only on change?**
The best bid, best ask and spread over time. A row is written only when a best price changes, as in real top-of-book
feeds; for 1,000,000 commands that is about 13,000 rows instead of a million. It has prices but not sizes.

**What did adding timestamps cost, and what did you try about it?**
`Order` grew from 32 to 40 bytes. Measured twice: throughput between -3% and +1% (inside the noise), median latency
+5 to 7 ns, p99 +7 to 10%. I tried packing the members and using 1-byte enums to get back to 32 bytes: no measurable
gain, so I did not adopt it. The lesson: an `enum class` is 4 bytes by default, so check `sizeof` instead of assuming.

**Where is the matching engine module from the synopsis?**
It is `OrderBook::match`, a private function of the book class, because matching must edit the price levels
directly. The synopsis listed it as a module; the code keeps it inside for simplicity (see `DESIGN.md`, section 8).

**What are the limitations?**
Single-threaded; limit and market orders only; the snapshot log has prices but no sizes; the duplicate-id check
covers resting orders only; id 0 reserved.

**How would you make it multithreaded?**
Not by locking the book: matching must be deterministic and locks add latency. Typical design: one thread owns the
book; input arrives through a lock-free single-producer single-consumer queue; scale by giving each instrument its
own book and thread.

**Why C++?**
Control over memory layout and allocation, and no garbage-collection pauses, so latency is predictable.

**How do real exchanges differ?**
More order types and auctions, other matching rules (for example pro-rata), network protocols, persistence and
recovery, risk checks, and regulation.

**What would you do next?**
Flat array of price levels, intrusive list instead of `std::list`, pre-sized index, sizes in the snapshot log, then a
multithreaded pipeline.

## 4. Practice (do these before the viva)

1. Write six orders on paper, predict every trade and the final book, then check with `./build/nanobook <file> out.csv --book`.
2. Write `match()` as pseudocode from memory, then compare with `docs/DESIGN.md`, section 5.
3. For each file in `src/` and `include/nanobook/`, say in one sentence what it is for.
4. Run `nanobook_bench` with 100000, 1000000 and 5000000 commands; explain why the numbers differ.
5. Make a small change yourself (print the spread after each command), then break a rule on purpose and find which test fails.
6. For one optimization, write your prediction before measuring, then compare it with the result.
7. Explain the identity-hash failure to a friend without looking at the notes.
8. Work out `sizeof(Order)` by hand (member sizes plus padding), then check it with a one-line program.
9. Run `nanobook` on `data/input_timestamps.csv` and on `data/input.csv`; explain every difference in the two trade files.
