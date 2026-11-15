#include "nanobook/snapshot_log.hpp"

#include <ostream>

namespace nanobook {

SnapshotLogger::SnapshotLogger(std::ostream& out) : out_(out) {
    out_ << "timestamp,command,best_bid,best_ask,spread\n";
}

bool SnapshotLogger::record(Timestamp timestamp, std::uint64_t commandNumber,
                            std::optional<Price> bestBid, std::optional<Price> bestAsk) {
    if (haveLast_ && bestBid == lastBid_ && bestAsk == lastAsk_) return false;

    haveLast_ = true;
    lastBid_  = bestBid;
    lastAsk_  = bestAsk;

    out_ << timestamp << ',' << commandNumber << ',';
    if (bestBid) out_ << *bestBid;
    out_ << ',';
    if (bestAsk) out_ << *bestAsk;
    out_ << ',';
    if (bestBid && bestAsk) out_ << (*bestAsk - *bestBid);
    out_ << '\n';
    ++rows_;
    return true;
}

}  // namespace nanobook
