# NanoBook design notes

## 1. Concepts in two minutes

* An **order book** lists all waiting (resting) orders for one instrument. **Bids** are buy orders, **asks**
  are sell orders. The highest bid is the **best bid**, the lowest ask is the **best ask**; the gap between
  them is the **spread**.
* A **limit order** says "buy/sell at this price or better". If it can trade immediately it does; whatever
  is left **rests** in the book. A **market order** says "trade now at any price"; whatever cannot trade
  is discarded.
* **Price-time priority** decides who trades first: better price first; at the same price, the order that
  arrived first.

Worked example (this is the first demo of the project):

| Step | Order | Result |
|---|---|---|
| 1 | #1 SELL 10 @ 101 | rests |
| 2 | #2 SELL 5 @ 102 | rests |
| 3 | #3 SELL 5 @ 101 | rests, behind #1 at the same price |
| 4 | #4 BUY 10 @ 99 | rests (99 < best ask 101, no trade) |
| 5 | #5 BUY 12 @ 101 | trades 10 with #1, then 2 with #3, both at 101; #3 keeps 3 |

## 2. Architecture and the synopsis modules

| Synopsis module | Where it is in the code | Note |
|---|---|---|
| Order Management (Order struct, validation) | `types.hpp`; validation in `OrderBook::addOrder`, `modify` and in `parseCommand` | `Order` and `Trade` carry an informational timestamp (section 3) |
| Order Book | `OrderBook` (`order_book.hpp/.cpp`) | bids, asks, id index |
| Matching Engine | `OrderBook::match` (private) | kept inside the book class: matching must edit the price levels directly, and splitting them would need a wide internal interface for no benefit |
| I/O | `csv_io.hpp/.cpp`, `main.cpp` | parser, trade writer, CLI |
| Benchmarking | `workload.cpp`, `bench.cpp`, `scripts/compare.py` | |

Data flow: `CSV line -> parseCommand -> Command -> applyCommand -> OrderBook -> vector<Trade> -> writeTrade -> CSV`.
`applyCommand` (in `engine.hpp`) is the single place that turns a command into a book call, shared by the CLI,
the benchmark and the tests.

## 3. Data structures

```
bids_  : map<Price, Level>  ordered high -> low    begin() = best bid
asks_  : map<Price, Level>  ordered low  -> high   begin() = best ask
Level  : list<Order>        front = oldest order at that price (FIFO)
index_ : FlatIdMap<OrderId -> Location{side, price, iterator into its Level}>
```

* **Integer prices** (ticks): floating-point numbers cannot be compared reliably for equality, and matching is
  all comparisons.
* **`std::list` per level:** removing an order from the middle (cancel) is O(1) given its iterator, and list
  iterators stay valid when other elements are added or removed. That is what lets `index_` store them.
* **`index_`:** without it, a cancel would have to search every level.
* **`Order` stays in the list; `Location` stays in the index.** They are kept consistent by `rest`, `match`
  and `cancel` (the only functions that touch both).

### Timestamps and the snapshot log

* `Timestamp` is a 64-bit number in whatever unit the input uses (for example nanoseconds since market open).
  A command's timestamp becomes the `Order`'s arrival time. A trade's timestamp is the arrival time of the
  **incoming** (aggressor) order, because that is the moment the trade happened. A modify that re-adds an order
  uses the modify's timestamp (0 means "not given": the old one is kept).
* Timestamps are **never used for priority.** Priority is the position in the FIFO queue. Timestamps can tie (two
  orders in the same nanosecond), arrive out of order in real feeds, or be missing; the queue order is exact
  and free. This is covered by a test where the earlier-arriving order has the later timestamp.
* If the input has no timestamp column, the command-line program uses the command's sequence number (1, 2, 3, ...),
  so output files are deterministic and reproducible.
* `SnapshotLogger` writes `timestamp,command,best_bid,best_ask,spread`, but only when the best bid or best ask
  **changes** (and for the first command). This is how top-of-book feeds behave, and it keeps the file small:
  in the 1,000,000-command benchmark workload it wrote about 13,000 rows. It records prices only, not sizes.

## 4. Complexity

P = price levels on one side, k = number of fills a command causes.

| Operation | Cost |
|---|---|
| best bid / best ask | O(1) |
| add order (matching part) | O(k): each fill is O(1); emptied levels are erased |
| add order (resting the remainder) | O(log P) to find or create the level, O(1) list append, O(1) expected hash insert |
| cancel | O(1) expected hash lookup + O(1) list erase + O(log P) to find the level (needed to delete it when empty) |
| modify, same price and quantity not increased | O(1) expected (edited in place) |
| modify, anything else | cancel + add |

Memory is O(N) for N resting orders.

## 5. Algorithms

**Matching an incoming BUY** (SELL is symmetric, with the sides swapped):

```
while incoming.quantity > 0 and asks not empty:
    level = lowest ask level
    if incoming is a limit order and level.price > incoming.price: stop
    while incoming.quantity > 0 and level not empty:
        resting = oldest order in level
        fill = min(incoming.quantity, resting.quantity)
        record trade(buy = incoming, sell = resting, price = level.price, quantity = fill)
        reduce both quantities; if resting is now 0, remove it from level and from the index
    if level is empty: remove the level
if incoming is a limit order and quantity > 0: rest it (append to its level, add to the index)
```

**Cancel:** look the id up in the index; erase the order from its level using the stored iterator; erase the level
if it became empty; erase the index entry.

**Modify:** if the price is unchanged and the quantity does not increase, edit in place (keeps queue position).
Otherwise cancel and re-add, which sends the order to the back of the queue and may trade immediately if the new
price crosses the book. This mirrors how real exchanges treat modifications: only a pure size reduction keeps priority.

## 6. Invariants (what must always be true)

1. The book is never crossed: best bid < best ask after every command.
2. `index_` holds exactly the resting orders, each pointing at its own list node.
3. Orders within a level are in arrival order; priority is lost only by an explicit modify.
4. Every resting order has quantity > 0 and a limit price > 0; no empty level is ever kept in a map.
5. A trade always executes at the resting order's price and carries the incoming order's timestamp.

The randomized test (`test_order_book.cpp`) checks invariant 1 after every command, and invariant 2 indirectly:
the number of resting orders, the best prices and the result of every cancel and modify must match the reference model.

## 7. How correctness is checked

* Hand-written cases for each rule (full and partial fills, price and time priority, cancel, modify, market
  orders, rejected input).
* **Differential test:** 20,000 random commands are fed to `OrderBook` and to a slow but obviously correct
  `ReferenceBook` (one flat vector); results must match at every step.
* **Mutation check:** the engine and the hash table were deliberately broken in several ways to confirm that the
  tests fail. This found one gap (untested wrap-around in the hash table), now covered.
* The two memory-sensitive suites were also run under AddressSanitizer and UndefinedBehaviorSanitizer.
* The same mutation check on the Step 8 code found that the glue function `applyCommand` was untested for timestamps
  (the other tests call `OrderBook` directly); a test now covers it.

## 8. Differences from the original synopsis (be upfront about these)

| Synopsis | Reality | Reason |
|---|---|---|
| `Order` has a timestamp | implemented (Step 8): `Order`, `Command` and `Trade` carry a `Timestamp` | informational only; priority still uses arrival order; measured cost is a few percent, see the optimization log |
| optional best bid/ask snapshot log | implemented: `--snapshots FILE`, a row whenever the best prices change | prices only; the quantity at the best prices is not recorded |
| separate "matching engine module" | a private function of `OrderBook` | see section 2 |
| (not in synopsis) | custom pool allocator and flat hash table | added in the optimization step because the measurements justified it |
| (not in synopsis) | order id 0 is rejected | 0 marks an empty slot in the flat hash table |
| "optionally GoogleTest" | a tiny built-in test harness | no dependency; `assert` is unusable in Release builds |

## 9. Performance design

See [`optimization_log.md`](optimization_log.md). In short: each change was a hypothesis, measured with interleaved
A/B runs, and kept or rejected on the numbers; one idea (identity hash) was rejected after it made things worse.

## 10. Limitations

Single-threaded; no persistence or network; only limit and market orders; no self-trade prevention; the duplicate
id check covers resting orders only (the id of a filled order can be reused); no tick-size or price-band checks;
prices and quantities have fixed widths (64-bit price, 32-bit quantity).
