#pragma once
#include <cstddef>
#include <functional>
#include <iosfwd>
#include <list>
#include <map>
#include <optional>
#include <vector>

#include "nanobook/flat_id_map.hpp"
#include "nanobook/pool_allocator.hpp"
#include "nanobook/types.hpp"

namespace nanobook {

class OrderBook {
public:
    OrderBook() = default;
    // Copying would leave list iterators pointing into the ORIGINAL book, so forbid it.
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    // Returns false if rejected (zero id, zero quantity, non-positive limit price, duplicate ID).
    // Trades caused by this order are appended to `trades`.
    bool addOrder(Order order, std::vector<Trade>& trades);

    // Removes a resting order. False if the ID is unknown.
    bool cancel(OrderId id);

    // Same price and quantity not increased: edited in place (keeps queue position).
    // Otherwise cancel + re-add (back of the queue; may match immediately).
    // `timestamp` is the time of the modify request: a re-added order gets it as its new arrival
    // time, and so do any trades it causes. 0 means "not given": the order keeps its old timestamp.
    bool modify(OrderId id, Price newPrice, Quantity newQty, std::vector<Trade>& trades,
                Timestamp timestamp = 0);

    std::optional<Price> bestBid() const;   // empty if no bids
    std::optional<Price> bestAsk() const;   // empty if no asks
    std::size_t restingOrderCount() const { return index_.size(); }
    void print(std::ostream& os) const;

private:
    // FIFO queue of orders at one price. Its nodes come from a pool (see pool_allocator.hpp).
    using Level = std::list<Order, PoolAllocator<Order>>;

    struct Location {
        Side            side;
        Price           price;
        Level::iterator it;
    };

    template <typename Compare>
    using LevelMap = std::map<Price, Level, Compare,
                              PoolAllocator<std::pair<const Price, Level>>>;

    LevelMap<std::greater<Price>> bids_;
    LevelMap<std::less<Price>>    asks_;
    FlatIdMap<Location>           index_;   // order ID -> where it sits (flat hash table)

    void rest(const Order& order);

    template <typename BookSide>
    static void removeFromBook(BookSide& side, const Location& loc);

    template <typename BookSide, typename Crosses>
    void match(Order& incoming, BookSide& opposite,
               std::vector<Trade>& trades, Crosses crosses);

    static void printLevel(std::ostream& os, Price price, const Level& level);
};

}  // namespace nanobook
