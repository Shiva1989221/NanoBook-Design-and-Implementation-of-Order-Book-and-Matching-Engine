#include "nanobook/workload.hpp"

#include <algorithm>
#include <random>

namespace nanobook {

std::vector<Command> generateWorkload(std::size_t count, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    auto pick = [&](std::int64_t lo, std::int64_t hi) {
        return std::uniform_int_distribution<std::int64_t>(lo, hi)(rng);
    };

    std::vector<Command> commands;
    commands.reserve(count);

    Price   mid    = 10000;      // 100.00 if one tick = 0.01
    OrderId nextId = 1;
    const OrderId window = 2000; // cancels/modifies target recent ids, so most are still resting

    // Price `offset` ticks away from mid on the passive side of `side`.
    // Negative offset = priced through the mid = aggressive (likely to trade at once).
    auto priceFor = [&](Side side, std::int64_t offset) {
        Price p = (side == Side::Buy) ? mid - offset : mid + offset;
        return std::max<Price>(p, 1);
    };

    while (commands.size() < count) {
        // The "fair price" does a slow random walk.
        if (pick(0, 9) == 0) mid = std::clamp<Price>(mid + pick(-1, 1), 9000, 11000);

        std::int64_t roll = pick(0, 99);
        Command c;  // defaults: Add, Buy, Limit, 0, 0 (cancel/modify leave unused fields default)

        if (roll < 70 || nextId == 1) {                    // limit order
            c.action   = Action::Add;
            c.id       = nextId++;
            c.side     = pick(0, 1) ? Side::Buy : Side::Sell;
            c.type     = OrderType::Limit;
            c.price    = priceFor(c.side, pick(-3, 30));
            c.quantity = static_cast<Quantity>(pick(1, 100));
        } else if (roll < 75) {                            // market order
            c.action   = Action::Add;
            c.id       = nextId++;
            c.side     = pick(0, 1) ? Side::Buy : Side::Sell;
            c.type     = OrderType::Market;
            c.quantity = static_cast<Quantity>(pick(1, 50));
        } else {
            OrderId lowest = nextId > window ? nextId - window : 1;
            c.id = static_cast<OrderId>(pick(static_cast<std::int64_t>(lowest),
                                             static_cast<std::int64_t>(nextId - 1)));
            if (roll < 95) {                               // cancel
                c.action = Action::Cancel;
            } else {                                       // modify
                c.action   = Action::Modify;
                c.price    = std::max<Price>(mid + pick(-30, 30), 1);
                c.quantity = static_cast<Quantity>(pick(1, 100));
            }
        }
        c.timestamp = static_cast<Timestamp>(commands.size() + 1) * 1000;   // 1 microsecond apart
        commands.push_back(c);
    }
    return commands;
}

}  // namespace nanobook
