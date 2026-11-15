#include "nanobook/order_book.hpp"

#include <algorithm>
#include <ostream>

namespace nanobook {

// Templates are defined here (not in the header) because only this file uses them.
template <typename BookSide>
void OrderBook::removeFromBook(BookSide& side, const Location& loc) {
    auto levelIt = side.find(loc.price);
    levelIt->second.erase(loc.it);
    if (levelIt->second.empty()) side.erase(levelIt);
}

template <typename BookSide, typename Crosses>
void OrderBook::match(Order& incoming, BookSide& opposite,
                      std::vector<Trade>& trades, Crosses crosses) {
    while (incoming.quantity > 0 && !opposite.empty()) {
        auto levelIt = opposite.begin();
        if (!crosses(levelIt->first)) break;

        Level& level = levelIt->second;
        while (incoming.quantity > 0 && !level.empty()) {
            Order& resting = level.front();
            Quantity fill = std::min(incoming.quantity, resting.quantity);

            if (incoming.side == Side::Buy)
                trades.push_back({incoming.id, resting.id, levelIt->first, fill, incoming.timestamp});
            else
                trades.push_back({resting.id, incoming.id, levelIt->first, fill, incoming.timestamp});

            incoming.quantity -= fill;
            resting.quantity  -= fill;
            if (resting.quantity == 0) {
                index_.erase(resting.id);  // before pop_front: `resting` dies with it
                level.pop_front();
            }
        }
        if (level.empty()) opposite.erase(levelIt);
    }
}

bool OrderBook::addOrder(Order order, std::vector<Trade>& trades) {
    if (order.id == 0) return false;   // 0 is reserved: it marks an empty slot in the index
    if (order.quantity == 0) return false;
    if (order.type == OrderType::Limit && order.price <= 0) return false;
    if (index_.find(order.id) != nullptr) return false;

    if (order.side == Side::Buy) {
        match(order, asks_, trades, [&](Price askPrice) {
            return order.type == OrderType::Market || askPrice <= order.price;
        });
    } else {
        match(order, bids_, trades, [&](Price bidPrice) {
            return order.type == OrderType::Market || bidPrice >= order.price;
        });
    }

    if (order.type == OrderType::Limit && order.quantity > 0) rest(order);
    return true;
}

bool OrderBook::cancel(OrderId id) {
    const Location* found = index_.find(id);
    if (found == nullptr) return false;

    Location loc = *found;   // copy: erasing from the index may move entries around
    if (loc.side == Side::Buy) removeFromBook(bids_, loc);
    else                       removeFromBook(asks_, loc);
    index_.erase(id);
    return true;
}

bool OrderBook::modify(OrderId id, Price newPrice, Quantity newQty,
                       std::vector<Trade>& trades, Timestamp timestamp) {
    if (newQty == 0 || newPrice <= 0) return false;
    const Location* found = index_.find(id);
    if (found == nullptr) return false;

    Location loc = *found;  // copy: the index entry is gone once we cancel
    if (newPrice == loc.price && newQty <= loc.it->quantity) {
        loc.it->quantity = newQty;
        return true;
    }

    Order order = *loc.it;
    cancel(id);
    order.price    = newPrice;
    order.quantity = newQty;
    if (timestamp != 0) order.timestamp = timestamp;
    return addOrder(order, trades);
}

std::optional<Price> OrderBook::bestBid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::bestAsk() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

void OrderBook::rest(const Order& order) {
    Level& level = (order.side == Side::Buy) ? bids_[order.price] : asks_[order.price];
    auto it = level.insert(level.end(), order);
    index_.insert(order.id, Location{order.side, order.price, it});
}

void OrderBook::print(std::ostream& os) const {
    os << "  ASKS (best = lowest, nearest the line)\n";
    for (auto it = asks_.rbegin(); it != asks_.rend(); ++it) printLevel(os, it->first, it->second);
    os << "  ------\n";
    for (auto it = bids_.begin(); it != bids_.end(); ++it) printLevel(os, it->first, it->second);
    os << "  BIDS (best = highest, nearest the line)\n";
}

void OrderBook::printLevel(std::ostream& os, Price price, const Level& level) {
    Quantity total = 0;
    for (const Order& o : level) total += o.quantity;
    os << "  " << price << "  x " << total << "  (" << level.size() << " orders)\n";
}

}  // namespace nanobook
