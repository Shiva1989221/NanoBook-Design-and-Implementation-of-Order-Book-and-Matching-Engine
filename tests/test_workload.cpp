#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "nanobook/csv_io.hpp"
#include "nanobook/engine.hpp"
#include "nanobook/workload.hpp"

using namespace nanobook;

namespace {

bool sameCommand(const Command& a, const Command& b) {
    return a.action == b.action && a.id == b.id && a.side == b.side &&
           a.type == b.type && a.price == b.price && a.quantity == b.quantity &&
           a.timestamp == b.timestamp;
}

bool sameWorkload(const std::vector<Command>& a, const std::vector<Command>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!sameCommand(a[i], b[i])) return false;
    return true;
}

void test_workload_has_requested_size() {
    CHECK(generateWorkload(1, 1).size() == 1);
    CHECK(generateWorkload(5000, 1).size() == 5000);
}

void test_same_seed_gives_identical_workload() {
    CHECK(sameWorkload(generateWorkload(3000, 7), generateWorkload(3000, 7)));
}

void test_different_seed_gives_different_workload() {
    CHECK(!sameWorkload(generateWorkload(3000, 7), generateWorkload(3000, 8)));
}

void test_workload_commands_are_well_formed() {
    auto commands = generateWorkload(20000, 99);
    OrderId lastAddId = 0;
    Timestamp lastTimestamp = 0;
    bool allOk = true;
    bool sawAdd = false, sawMarket = false, sawCancel = false, sawModify = false;

    for (const Command& c : commands) {
        if (c.timestamp <= lastTimestamp) allOk = false;   // strictly increasing, never 0
        lastTimestamp = c.timestamp;
        switch (c.action) {
            case Action::Add:
                sawAdd = true;
                if (c.id != lastAddId + 1) allOk = false;       // ids are 1, 2, 3, ... in order
                lastAddId = c.id;
                if (c.quantity == 0) allOk = false;
                if (c.type == OrderType::Limit && c.price <= 0) allOk = false;
                if (c.type == OrderType::Market) sawMarket = true;
                break;
            case Action::Cancel:
                sawCancel = true;
                if (c.id == 0 || c.id > lastAddId) allOk = false;   // only targets issued ids
                break;
            case Action::Modify:
                sawModify = true;
                if (c.id == 0 || c.id > lastAddId) allOk = false;
                if (c.price <= 0 || c.quantity == 0) allOk = false;
                break;
        }
    }
    CHECK(allOk);
    CHECK(sawAdd && sawMarket && sawCancel && sawModify);   // all four kinds appear
}

void test_every_generated_command_round_trips_through_csv() {
    auto commands = generateWorkload(5000, 3);
    bool allMatch = true;
    for (const Command& original : commands) {
        std::ostringstream os;
        writeCommand(os, original);

        Command parsed;
        std::string error;
        if (parseCommand(os.str(), parsed, error) != ParseStatus::Ok ||
            !sameCommand(original, parsed)) {
            allMatch = false;
            break;
        }
    }
    CHECK(allMatch);
}

void test_write_command_format() {
    std::ostringstream os;
    writeCommand(os, Command{Action::Add, 1, Side::Sell, OrderType::Limit, 101, 10});
    writeCommand(os, Command{Action::Add, 9, Side::Buy, OrderType::Market, 0, 8});
    writeCommand(os, Command{Action::Cancel, 5, Side::Buy, OrderType::Limit, 0, 0});
    writeCommand(os, Command{Action::Modify, 2, Side::Buy, OrderType::Limit, 101, 6});
    CHECK(os.str() ==
          "ADD,1,SELL,LIMIT,101,10\n"
          "ADD,9,BUY,MARKET,,8\n"
          "CANCEL,5\n"
          "MODIFY,2,,,101,6\n");

    std::ostringstream withTs;   // with timestamps the optional last column appears
    writeCommand(withTs, Command{Action::Add, 1, Side::Sell, OrderType::Limit, 101, 10, 5000});
    writeCommand(withTs, Command{Action::Cancel, 5, Side::Buy, OrderType::Limit, 0, 0, 7000});
    writeCommand(withTs, Command{Action::Modify, 2, Side::Buy, OrderType::Limit, 101, 6, 8000});
    CHECK(withTs.str() ==
          "ADD,1,SELL,LIMIT,101,10,5000\n"
          "CANCEL,5,,,,,7000\n"
          "MODIFY,2,,,101,6,8000\n");
}

void test_apply_command_passes_timestamps_through_to_the_book() {
    OrderBook book;
    std::vector<Trade> trades;

    // ADD: the trade must carry the timestamp of the command that caused it.
    CHECK(applyCommand(book, Command{Action::Add, 1, Side::Sell, OrderType::Limit, 101, 5, 1000}, trades));
    CHECK(trades.empty());
    CHECK(applyCommand(book, Command{Action::Add, 2, Side::Buy, OrderType::Limit, 101, 5, 2000}, trades));
    CHECK(trades.size() == 1 && trades[0].timestamp == 2000);

    // MODIFY that crosses the book: same rule.
    CHECK(applyCommand(book, Command{Action::Add, 3, Side::Buy, OrderType::Limit, 99, 10, 3000}, trades));
    CHECK(applyCommand(book, Command{Action::Add, 4, Side::Sell, OrderType::Limit, 101, 4, 4000}, trades));
    trades.clear();
    CHECK(applyCommand(book, Command{Action::Modify, 3, Side::Buy, OrderType::Limit, 101, 10, 5000}, trades));
    CHECK(trades.size() == 1 && trades[0].timestamp == 5000);
}

void test_engine_handles_the_workload_sensibly() {
    auto commands = generateWorkload(30000, 42);
    OrderBook book;
    std::vector<Trade> trades;
    std::size_t rejected = 0, totalTrades = 0;
    bool neverCrossed = true;

    for (const Command& c : commands) {
        trades.clear();
        if (!applyCommand(book, c, trades)) ++rejected;
        totalTrades += trades.size();
        if (book.bestBid() && book.bestAsk() && !(*book.bestBid() < *book.bestAsk()))
            neverCrossed = false;
    }
    CHECK(neverCrossed);
    CHECK(totalTrades > 1000);              // the workload really does cause trading
    CHECK(rejected < commands.size() / 4);  // most commands are valid (some cancels hit filled orders)
    CHECK(book.restingOrderCount() > 0);
}

}  // namespace

int main() {
    RUN(test_workload_has_requested_size);
    RUN(test_same_seed_gives_identical_workload);
    RUN(test_different_seed_gives_different_workload);
    RUN(test_workload_commands_are_well_formed);
    RUN(test_every_generated_command_round_trips_through_csv);
    RUN(test_write_command_format);
    RUN(test_apply_command_passes_timestamps_through_to_the_book);
    RUN(test_engine_handles_the_workload_sensibly);
    return finish();
}
