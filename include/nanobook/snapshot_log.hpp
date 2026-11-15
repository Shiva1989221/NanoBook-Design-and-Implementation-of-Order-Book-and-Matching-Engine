#pragma once
#include <cstdint>
#include <iosfwd>
#include <optional>

#include "nanobook/types.hpp"

namespace nanobook {

// Writes a "top of book" history: the best bid and best ask over time.
//
//   timestamp,command,best_bid,best_ask,spread
//   1,1,,101,
//   4,4,99,101,2
//
// A row is written only when the best bid or the best ask CHANGES (and always for the very
// first call), not after every command. That is how top-of-book feeds work, and it keeps the
// file small: millions of commands usually move the best prices far less often.
// An empty side is written as an empty cell; the spread is empty unless both sides exist.
class SnapshotLogger {
public:
    explicit SnapshotLogger(std::ostream& out);   // writes the header row

    // Call after every command. Returns true if a row was written.
    bool record(Timestamp timestamp, std::uint64_t commandNumber,
                std::optional<Price> bestBid, std::optional<Price> bestAsk);

    std::uint64_t rowsWritten() const { return rows_; }

private:
    std::ostream&        out_;
    bool                 haveLast_ = false;
    std::optional<Price> lastBid_;
    std::optional<Price> lastAsk_;
    std::uint64_t        rows_ = 0;
};

}  // namespace nanobook
