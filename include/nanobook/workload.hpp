#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "nanobook/csv_io.hpp"

namespace nanobook {

// Generates `count` synthetic commands that look roughly like real order flow:
//   ~70% limit orders, mostly near a slowly drifting mid price (some cross the spread)
//    ~5% market orders
//   ~20% cancels of recently issued orders (some are already filled, so get rejected)
//    ~5% modifies of recently issued orders
// Commands carry increasing timestamps (1000 ns apart).
// The same (count, seed) always gives the same workload, so benchmark runs are comparable.
std::vector<Command> generateWorkload(std::size_t count, std::uint64_t seed);

}  // namespace nanobook
