#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

#include "check.hpp"
#include "nanobook/order_book.hpp"

using namespace nanobook;

namespace {

// ---------- helpers ----------

Order limitOrder(OrderId id, Side side, Price price, Quantity qty) {
    return Order{id, side, OrderType::Limit, price, qty};
}

Order marketOrder(OrderId id, Side side, Quantity qty) {
    return Order{id, side, OrderType::Market, 0, qty};
}

// Adds an order we expect to be accepted and returns the trades it caused.
std::vector<Trade> add(OrderBook& book, const Order& order) {
    std::vector<Trade> trades;
    bool accepted = book.addOrder(order, trades);
    CHECK(accepted);
    return trades;
}

bool isTrade(const Trade& t, OrderId buy, OrderId sell, Price price, Quantity qty) {
    return t.buyOrderId == buy && t.sellOrderId == sell &&
           t.price == price && t.quantity == qty;
}

// ---------- basic matching ----------

void test_empty_book() {
    OrderBook book;
    CHECK(!book.bestBid().has_value());
    CHECK(!book.bestAsk().has_value());
    CHECK(book.restingOrderCount() == 0);
}

void test_limit_order_rests_when_nothing_to_match() {
    OrderBook book;
    auto trades = add(book, limitOrder(1, Side::Buy, 99, 10));
    CHECK(trades.empty());
    CHECK(book.bestBid() == 99);
    CHECK(!book.bestAsk().has_value());
    CHECK(book.restingOrderCount() == 1);
}

void test_no_trade_when_prices_do_not_overlap() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 101, 5));
    auto trades = add(book, limitOrder(2, Side::Buy, 100, 5));
    CHECK(trades.empty());
    CHECK(book.bestBid() == 100);
    CHECK(book.bestAsk() == 101);
    CHECK(book.restingOrderCount() == 2);
}

void test_exact_full_fill() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 101, 10));
    auto trades = add(book, limitOrder(2, Side::Buy, 101, 10));
    CHECK(trades.size() == 1 && isTrade(trades[0], 2, 1, 101, 10));
    CHECK(book.restingOrderCount() == 0);
    CHECK(!book.bestBid().has_value());
    CHECK(!book.bestAsk().has_value());
}

void test_incoming_larger_than_resting_leaves_correct_remainder() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 101, 5));
    auto trades = add(book, limitOrder(2, Side::Buy, 101, 8));
    CHECK(trades.size() == 1 && isTrade(trades[0], 2, 1, 101, 5));
    CHECK(book.bestBid() == 101);
    CHECK(!book.bestAsk().has_value());
    CHECK(book.restingOrderCount() == 1);

    // The 3 left over must be exactly 3: sell 10 should only trade 3.
    trades = add(book, limitOrder(3, Side::Sell, 101, 10));
    CHECK(trades.size() == 1 && isTrade(trades[0], 2, 3, 101, 3));
    CHECK(book.bestAsk() == 101);   // 7 of order #3 left
    CHECK(book.restingOrderCount() == 1);
}

void test_resting_larger_than_incoming_stays_in_book() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 101, 10));
    auto trades = add(book, limitOrder(2, Side::Buy, 101, 4));
    CHECK(trades.size() == 1 && isTrade(trades[0], 2, 1, 101, 4));
    CHECK(book.restingOrderCount() == 1);

    trades = add(book, limitOrder(3, Side::Buy, 101, 6));
    CHECK(trades.size() == 1 && isTrade(trades[0], 3, 1, 101, 6));
    CHECK(book.restingOrderCount() == 0);
    CHECK(!book.bestAsk().has_value());
}

// ---------- priority rules ----------

void test_time_priority_within_a_price_level() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 101, 10));
    add(book, limitOrder(2, Side::Sell, 101, 10));
    auto trades = add(book, limitOrder(3, Side::Buy, 101, 12));
    CHECK(trades.size() == 2);
    CHECK(trades.size() == 2 && isTrade(trades[0], 3, 1, 101, 10));  // oldest first
    CHECK(trades.size() == 2 && isTrade(trades[1], 3, 2, 101, 2));
}

void test_price_priority_beats_arrival_time() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 102, 5));   // arrived first, worse price
    add(book, limitOrder(2, Side::Sell, 101, 5));   // arrived later, better price
    auto trades = add(book, limitOrder(3, Side::Buy, 102, 5));
    CHECK(trades.size() == 1 && isTrade(trades[0], 3, 2, 101, 5));
    CHECK(book.bestAsk() == 102);
}

void test_trade_price_is_the_resting_orders_price() {
    {
        OrderBook book;
        add(book, limitOrder(1, Side::Sell, 100, 5));
        auto trades = add(book, limitOrder(2, Side::Buy, 105, 5));
        CHECK(trades.size() == 1 && isTrade(trades[0], 2, 1, 100, 5));
    }
    {
        OrderBook book;
        add(book, limitOrder(1, Side::Buy, 105, 5));
        auto trades = add(book, limitOrder(2, Side::Sell, 100, 5));
        CHECK(trades.size() == 1 && isTrade(trades[0], 1, 2, 105, 5));
    }
}

void test_order_sweeps_multiple_price_levels() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 101, 3));
    add(book, limitOrder(2, Side::Sell, 102, 3));
    add(book, limitOrder(3, Side::Sell, 103, 3));
    auto trades = add(book, limitOrder(4, Side::Buy, 102, 10));
    CHECK(trades.size() == 2);
    CHECK(trades.size() == 2 && isTrade(trades[0], 4, 1, 101, 3));
    CHECK(trades.size() == 2 && isTrade(trades[1], 4, 2, 102, 3));
    CHECK(book.bestBid() == 102);        // remaining 4 rests at its own limit price
    CHECK(book.bestAsk() == 103);        // 103 is beyond the limit: untouched
    CHECK(book.restingOrderCount() == 2);
}

// ---------- cancel ----------

void test_cancel_resting_order() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 101, 5));
    CHECK(book.cancel(1));
    CHECK(book.restingOrderCount() == 0);
    CHECK(!book.bestAsk().has_value());
    CHECK(!book.cancel(1));      // already cancelled
    CHECK(!book.cancel(999));    // never existed

    auto trades = add(book, limitOrder(2, Side::Buy, 101, 5));
    CHECK(trades.empty());       // cancelled order must not be matched
    CHECK(book.bestBid() == 101);
}

void test_cancel_one_of_two_orders_at_same_level() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 101, 5));
    add(book, limitOrder(2, Side::Sell, 101, 5));
    CHECK(book.cancel(1));
    CHECK(book.bestAsk() == 101);          // level still exists
    CHECK(book.restingOrderCount() == 1);
    auto trades = add(book, limitOrder(3, Side::Buy, 101, 5));
    CHECK(trades.size() == 1 && isTrade(trades[0], 3, 2, 101, 5));
}

void test_cancel_filled_order_fails() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 101, 5));
    add(book, limitOrder(2, Side::Buy, 101, 5));    // fully fills #1, #2 never rests
    CHECK(!book.cancel(1));
    CHECK(!book.cancel(2));
}

void test_cancel_partially_filled_order() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 101, 10));
    add(book, limitOrder(2, Side::Buy, 101, 4));
    CHECK(book.cancel(1));
    CHECK(book.restingOrderCount() == 0);
    CHECK(!book.bestAsk().has_value());
}

// ---------- modify ----------

void test_modify_reduce_quantity_keeps_priority() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 101, 10));
    add(book, limitOrder(2, Side::Sell, 101, 10));
    std::vector<Trade> none;
    CHECK(book.modify(1, 101, 5, none));
    CHECK(none.empty());
    auto trades = add(book, limitOrder(3, Side::Buy, 101, 7));
    CHECK(trades.size() == 2);
    CHECK(trades.size() == 2 && isTrade(trades[0], 3, 1, 101, 5));  // still first
    CHECK(trades.size() == 2 && isTrade(trades[1], 3, 2, 101, 2));
}

void test_modify_increase_quantity_loses_priority() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 101, 10));
    add(book, limitOrder(2, Side::Sell, 101, 10));
    std::vector<Trade> none;
    CHECK(book.modify(1, 101, 12, none));
    auto trades = add(book, limitOrder(3, Side::Buy, 101, 10));
    CHECK(trades.size() == 1 && isTrade(trades[0], 3, 2, 101, 10));  // #2 is now first
    trades = add(book, limitOrder(4, Side::Buy, 101, 12));
    CHECK(trades.size() == 1 && isTrade(trades[0], 4, 1, 101, 12));
}

void test_modify_price_moves_the_order() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 101, 5));
    std::vector<Trade> none;
    CHECK(book.modify(1, 103, 5, none));
    CHECK(book.bestAsk() == 103);
    auto trades = add(book, limitOrder(2, Side::Buy, 102, 5));
    CHECK(trades.empty());
}

void test_modify_price_that_crosses_trades_immediately() {
    OrderBook book;
    add(book, limitOrder(1, Side::Buy, 99, 10));
    add(book, limitOrder(2, Side::Sell, 101, 4));
    std::vector<Trade> trades;
    CHECK(book.modify(1, 101, 10, trades));
    CHECK(trades.size() == 1 && isTrade(trades[0], 1, 2, 101, 4));
    CHECK(book.bestBid() == 101);               // remaining 6 rests at the new price
    CHECK(!book.bestAsk().has_value());
    CHECK(book.restingOrderCount() == 1);
}

void test_modify_invalid_requests_change_nothing() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 101, 5));
    std::vector<Trade> none;
    CHECK(!book.modify(999, 101, 5, none));   // unknown id
    CHECK(!book.modify(1, 101, 0, none));     // zero quantity
    CHECK(!book.modify(1, 0, 5, none));       // zero price
    CHECK(!book.modify(1, -3, 5, none));      // negative price
    CHECK(book.bestAsk() == 101);
    auto trades = add(book, limitOrder(2, Side::Buy, 101, 5));
    CHECK(trades.size() == 1 && isTrade(trades[0], 2, 1, 101, 5));  // order intact
}

// ---------- market orders ----------

void test_market_buy_sweeps_levels_and_discards_remainder() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 101, 3));
    add(book, limitOrder(2, Side::Sell, 102, 3));
    auto trades = add(book, marketOrder(3, Side::Buy, 10));
    CHECK(trades.size() == 2);
    CHECK(trades.size() == 2 && isTrade(trades[0], 3, 1, 101, 3));
    CHECK(trades.size() == 2 && isTrade(trades[1], 3, 2, 102, 3));
    CHECK(book.restingOrderCount() == 0);       // the 4 left over is discarded
    CHECK(!book.bestBid().has_value());
}

void test_market_order_on_empty_book() {
    OrderBook book;
    auto trades = add(book, marketOrder(1, Side::Buy, 5));
    CHECK(trades.empty());
    CHECK(book.restingOrderCount() == 0);
    CHECK(!book.bestBid().has_value());
}

void test_market_sell_hits_best_bid_first() {
    OrderBook book;
    add(book, limitOrder(1, Side::Buy, 99, 5));
    add(book, limitOrder(2, Side::Buy, 100, 5));
    auto trades = add(book, marketOrder(3, Side::Sell, 7));
    CHECK(trades.size() == 2);
    CHECK(trades.size() == 2 && isTrade(trades[0], 2, 3, 100, 5));
    CHECK(trades.size() == 2 && isTrade(trades[1], 1, 3, 99, 2));
}

// ---------- validation ----------

void test_rejects_invalid_orders() {
    OrderBook book;
    std::vector<Trade> trades;
    CHECK(!book.addOrder(limitOrder(1, Side::Buy, 100, 0), trades));    // zero quantity
    CHECK(!book.addOrder(limitOrder(2, Side::Buy, 0, 5), trades));      // zero price
    CHECK(!book.addOrder(limitOrder(3, Side::Buy, -7, 5), trades));     // negative price
    CHECK(!book.addOrder(marketOrder(4, Side::Buy, 0), trades));        // zero quantity
    CHECK(trades.empty());
    CHECK(book.restingOrderCount() == 0);
}

void test_rejects_order_id_zero() {
    OrderBook book;
    std::vector<Trade> trades;
    CHECK(!book.addOrder(limitOrder(0, Side::Buy, 100, 5), trades));
    CHECK(!book.addOrder(marketOrder(0, Side::Sell, 5), trades));
    CHECK(!book.cancel(0));
    CHECK(!book.modify(0, 100, 5, trades));
    CHECK(trades.empty());
    CHECK(book.restingOrderCount() == 0);
}

void test_rejects_duplicate_id_of_resting_order() {
    OrderBook book;
    add(book, limitOrder(1, Side::Sell, 101, 5));
    std::vector<Trade> trades;
    CHECK(!book.addOrder(limitOrder(1, Side::Buy, 105, 5), trades));
    CHECK(trades.empty());                       // the rejected order must not trade
    CHECK(book.restingOrderCount() == 1);
    trades = add(book, limitOrder(2, Side::Buy, 101, 5));
    CHECK(trades.size() == 1 && isTrade(trades[0], 2, 1, 101, 5));   // original untouched
}

// ---------- timestamps ----------

void test_trade_carries_the_incoming_orders_timestamp() {
    OrderBook book;
    add(book, Order{1, Side::Sell, OrderType::Limit, 101, 5, 1000});
    auto trades = add(book, Order{2, Side::Buy, OrderType::Limit, 101, 5, 2500});
    // The trade happens when the buyer ARRIVES (2500), not when the seller did (1000).
    CHECK(trades.size() == 1 && trades[0].timestamp == 2500);
}

void test_every_trade_of_one_command_gets_the_same_timestamp() {
    OrderBook book;
    add(book, Order{1, Side::Sell, OrderType::Limit, 101, 3, 100});
    add(book, Order{2, Side::Sell, OrderType::Limit, 102, 3, 200});
    auto trades = add(book, Order{3, Side::Buy, OrderType::Limit, 102, 6, 9000});
    CHECK(trades.size() == 2);
    CHECK(trades.size() == 2 && trades[0].timestamp == 9000 && trades[1].timestamp == 9000);
}

void test_priority_follows_arrival_order_not_timestamps() {
    OrderBook book;
    add(book, Order{1, Side::Sell, OrderType::Limit, 101, 5, 5000});   // arrives first, LATER timestamp
    add(book, Order{2, Side::Sell, OrderType::Limit, 101, 5, 1000});   // arrives second, EARLIER timestamp
    auto trades = add(book, Order{3, Side::Buy, OrderType::Limit, 101, 5, 6000});
    CHECK(trades.size() == 1 && isTrade(trades[0], 3, 1, 101, 5));    // queue position decides
}

void test_modify_that_crosses_uses_the_modify_timestamp() {
    OrderBook book;
    add(book, Order{1, Side::Buy, OrderType::Limit, 99, 10, 100});
    add(book, Order{2, Side::Sell, OrderType::Limit, 101, 4, 200});
    std::vector<Trade> trades;
    CHECK(book.modify(1, 101, 10, trades, 900));
    CHECK(trades.size() == 1 && trades[0].timestamp == 900);
}

void test_modify_without_timestamp_keeps_the_old_one() {
    OrderBook book;
    add(book, Order{1, Side::Buy, OrderType::Limit, 99, 10, 100});
    add(book, Order{2, Side::Sell, OrderType::Limit, 101, 4, 200});
    std::vector<Trade> trades;
    CHECK(book.modify(1, 101, 10, trades));          // no timestamp given
    CHECK(trades.size() == 1 && trades[0].timestamp == 100);
}

// ---------- large burst ----------

void test_large_burst_sweep() {
    OrderBook book;
    const int n = 10000;
    for (int i = 1; i <= n; ++i)
        add(book, limitOrder(static_cast<OrderId>(i), Side::Sell, 100 + (i % 10), 1));
    CHECK(book.restingOrderCount() == static_cast<std::size_t>(n));

    auto trades = add(book, limitOrder(n + 1, Side::Buy, 200, n));
    CHECK(trades.size() == static_cast<std::size_t>(n));

    std::uint64_t totalQty = 0;
    bool ordered = true;
    for (std::size_t i = 0; i < trades.size(); ++i) {
        totalQty += trades[i].quantity;
        if (i > 0) {
            const Trade& prev = trades[i - 1];
            const Trade& cur  = trades[i];
            if (cur.price < prev.price) ordered = false;                       // best price first
            if (cur.price == prev.price && cur.sellOrderId < prev.sellOrderId) // oldest first
                ordered = false;
        }
    }
    CHECK(totalQty == static_cast<std::uint64_t>(n));
    CHECK(ordered);
    CHECK(book.restingOrderCount() == 0);
}

// ---------- randomized test against a deliberately naive reference ----------
//
// ReferenceBook is slow but obviously correct: one flat vector in arrival order.
// "Best price, then earliest" = scan the vector and keep the first strictly-better price.
// We feed the same random commands to it and to the real OrderBook and demand
// identical results every step. This finds bugs that hand-written cases miss.

class ReferenceBook {
public:
    bool add(Order order, std::vector<Trade>& trades) {
        if (order.id == 0) return false;
        if (order.quantity == 0) return false;
        if (order.type == OrderType::Limit && order.price <= 0) return false;
        if (find(order.id) != orders_.end()) return false;

        while (order.quantity > 0) {
            auto best = orders_.end();
            for (auto it = orders_.begin(); it != orders_.end(); ++it) {
                if (it->side == order.side) continue;
                bool crosses = order.type == OrderType::Market ||
                               (order.side == Side::Buy ? it->price <= order.price
                                                        : it->price >= order.price);
                if (!crosses) continue;
                bool better = best == orders_.end() ||
                              (order.side == Side::Buy ? it->price < best->price
                                                       : it->price > best->price);
                if (better) best = it;
            }
            if (best == orders_.end()) break;

            Quantity fill = std::min(order.quantity, best->quantity);
            if (order.side == Side::Buy)
                trades.push_back({order.id, best->id, best->price, fill});
            else
                trades.push_back({best->id, order.id, best->price, fill});
            order.quantity -= fill;
            best->quantity -= fill;
            if (best->quantity == 0) orders_.erase(best);
        }
        if (order.type == OrderType::Limit && order.quantity > 0) orders_.push_back(order);
        return true;
    }

    bool cancel(OrderId id) {
        auto it = find(id);
        if (it == orders_.end()) return false;
        orders_.erase(it);
        return true;
    }

    bool modify(OrderId id, Price newPrice, Quantity newQty, std::vector<Trade>& trades) {
        if (newQty == 0 || newPrice <= 0) return false;
        auto it = find(id);
        if (it == orders_.end()) return false;
        if (newPrice == it->price && newQty <= it->quantity) {
            it->quantity = newQty;
            return true;
        }
        Order order = *it;
        orders_.erase(it);
        order.price    = newPrice;
        order.quantity = newQty;
        return add(order, trades);
    }

    std::size_t size() const { return orders_.size(); }

    std::optional<Price> bestBid() const {
        std::optional<Price> best;
        for (const Order& o : orders_)
            if (o.side == Side::Buy && (!best || o.price > *best)) best = o.price;
        return best;
    }

    std::optional<Price> bestAsk() const {
        std::optional<Price> best;
        for (const Order& o : orders_)
            if (o.side == Side::Sell && (!best || o.price < *best)) best = o.price;
        return best;
    }

private:
    std::vector<Order>::iterator find(OrderId id) {
        return std::find_if(orders_.begin(), orders_.end(),
                            [id](const Order& o) { return o.id == id; });
    }
    std::vector<Order> orders_;   // arrival order = time priority
};

bool sameTrades(const std::vector<Trade>& a, const std::vector<Trade>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!isTrade(a[i], b[i].buyOrderId, b[i].sellOrderId, b[i].price, b[i].quantity))
            return false;
    return true;
}

void test_random_operations_match_reference_model() {
    std::mt19937_64 rng(12345);   // fixed seed: the test is repeatable
    auto pick = [&](int lo, int hi) {
        return std::uniform_int_distribution<int>(lo, hi)(rng);
    };

    OrderBook book;
    ReferenceBook ref;
    OrderId nextId = 1;
    std::size_t totalTrades = 0;

    for (int step = 0; step < 20000; ++step) {
        std::vector<Trade> got, expected;
        bool okGot = false, okExpected = false;
        int action = pick(0, 9);

        if (action <= 5) {                                   // 60% add
            OrderId id = nextId++;
            if (pick(0, 19) == 0)                            // 5%: reuse an old id
                id = static_cast<OrderId>(pick(1, static_cast<int>(nextId) - 1));
            Side side = pick(0, 1) ? Side::Buy : Side::Sell;
            Quantity qty = static_cast<Quantity>(pick(0, 20));   // 0 sometimes: invalid
            Order order = (pick(0, 9) == 0) ? marketOrder(id, side, qty)
                                            : limitOrder(id, side, pick(95, 105), qty);
            okGot      = book.addOrder(order, got);
            okExpected = ref.add(order, expected);
        } else if (action <= 7) {                            // 20% cancel
            OrderId id = static_cast<OrderId>(pick(1, static_cast<int>(nextId) + 2));
            okGot      = book.cancel(id);
            okExpected = ref.cancel(id);
        } else {                                             // 20% modify
            OrderId id = static_cast<OrderId>(pick(1, static_cast<int>(nextId) + 2));
            Price price = pick(0, 106);                      // 0 sometimes: invalid
            Quantity qty = static_cast<Quantity>(pick(0, 25));
            okGot      = book.modify(id, price, qty, got);
            okExpected = ref.modify(id, price, qty, expected);
        }

        bool same = okGot == okExpected && sameTrades(got, expected) &&
                    book.restingOrderCount() == ref.size() &&
                    book.bestBid() == ref.bestBid() && book.bestAsk() == ref.bestAsk();
        if (!same) {
            CHECK(same);
            std::cerr << "  first divergence at random step " << step << "\n";
            return;
        }

        // Invariant: the book is never crossed (best bid must be below best ask).
        if (book.bestBid() && book.bestAsk()) {
            bool uncrossed = *book.bestBid() < *book.bestAsk();
            if (!uncrossed) {
                CHECK(uncrossed);
                std::cerr << "  crossed book at random step " << step << "\n";
                return;
            }
        }
        totalTrades += got.size();
    }
    CHECK(totalTrades > 1000);   // make sure the test really exercised matching
}

}  // namespace

int main() {
    RUN(test_empty_book);
    RUN(test_limit_order_rests_when_nothing_to_match);
    RUN(test_no_trade_when_prices_do_not_overlap);
    RUN(test_exact_full_fill);
    RUN(test_incoming_larger_than_resting_leaves_correct_remainder);
    RUN(test_resting_larger_than_incoming_stays_in_book);
    RUN(test_time_priority_within_a_price_level);
    RUN(test_price_priority_beats_arrival_time);
    RUN(test_trade_price_is_the_resting_orders_price);
    RUN(test_order_sweeps_multiple_price_levels);
    RUN(test_cancel_resting_order);
    RUN(test_cancel_one_of_two_orders_at_same_level);
    RUN(test_cancel_filled_order_fails);
    RUN(test_cancel_partially_filled_order);
    RUN(test_modify_reduce_quantity_keeps_priority);
    RUN(test_modify_increase_quantity_loses_priority);
    RUN(test_modify_price_moves_the_order);
    RUN(test_modify_price_that_crosses_trades_immediately);
    RUN(test_modify_invalid_requests_change_nothing);
    RUN(test_market_buy_sweeps_levels_and_discards_remainder);
    RUN(test_market_order_on_empty_book);
    RUN(test_market_sell_hits_best_bid_first);
    RUN(test_rejects_invalid_orders);
    RUN(test_rejects_order_id_zero);
    RUN(test_rejects_duplicate_id_of_resting_order);
    RUN(test_trade_carries_the_incoming_orders_timestamp);
    RUN(test_every_trade_of_one_command_gets_the_same_timestamp);
    RUN(test_priority_follows_arrival_order_not_timestamps);
    RUN(test_modify_that_crosses_uses_the_modify_timestamp);
    RUN(test_modify_without_timestamp_keeps_the_old_one);
    RUN(test_large_burst_sweep);
    RUN(test_random_operations_match_reference_model);
    return finish();
}
