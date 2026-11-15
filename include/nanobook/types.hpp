#pragma once
#include <cstdint>

namespace nanobook {

using OrderId  = std::uint64_t;
using Price    = std::int64_t;   // integer ticks, never double
using Quantity = std::uint32_t;

// When something happened, in any increasing unit the input uses (for example nanoseconds
// since market open). It is INFORMATION for logs and analysis: who trades first is decided by
// arrival ORDER (position in the FIFO queue), never by comparing timestamps.
using Timestamp = std::uint64_t;

enum class Side { Buy, Sell };
enum class OrderType { Limit, Market };

struct Order {
    OrderId   id;
    Side      side;
    OrderType type;
    Price     price;      // ignored for market orders
    Quantity  quantity;   // remaining quantity
    Timestamp timestamp = 0;   // when the order arrived (0 = unknown)
};

struct Trade {
    OrderId  buyOrderId;
    OrderId  sellOrderId;
    Price    price;
    Quantity quantity;
    Timestamp timestamp = 0;   // time of the trade = arrival time of the incoming (aggressor) order
};

}  // namespace nanobook
