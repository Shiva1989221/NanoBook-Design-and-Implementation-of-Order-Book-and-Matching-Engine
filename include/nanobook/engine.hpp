#pragma once
#include <vector>

#include "nanobook/csv_io.hpp"
#include "nanobook/order_book.hpp"

namespace nanobook {

// Applies one parsed command to the book. Returns false if the book rejected it.
// Trades caused by the command are appended to `trades`.
// Shared by main (CSV replay), the benchmark and the tests.
inline bool applyCommand(OrderBook& book, const Command& cmd, std::vector<Trade>& trades) {
    switch (cmd.action) {
        case Action::Add:
            return book.addOrder(
                Order{cmd.id, cmd.side, cmd.type, cmd.price, cmd.quantity, cmd.timestamp}, trades);
        case Action::Cancel:
            return book.cancel(cmd.id);
        case Action::Modify:
            return book.modify(cmd.id, cmd.price, cmd.quantity, trades, cmd.timestamp);
    }
    return false;  // unreachable; keeps compilers quiet
}

}  // namespace nanobook
