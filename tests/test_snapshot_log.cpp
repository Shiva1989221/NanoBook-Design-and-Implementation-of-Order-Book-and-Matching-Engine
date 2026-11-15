#include <optional>
#include <sstream>
#include <string>

#include "check.hpp"
#include "nanobook/snapshot_log.hpp"

using namespace nanobook;

namespace {

using Opt = std::optional<Price>;
const std::string kHeader = "timestamp,command,best_bid,best_ask,spread\n";

void test_header_is_written_immediately() {
    std::ostringstream os;
    SnapshotLogger log(os);
    CHECK(os.str() == kHeader);
    CHECK(log.rowsWritten() == 0);
}

void test_first_call_always_writes_a_row() {
    std::ostringstream os;
    SnapshotLogger log(os);
    CHECK(log.record(10, 1, Opt(99), Opt(101)));
    CHECK(os.str() == kHeader + "10,1,99,101,2\n");
    CHECK(log.rowsWritten() == 1);
}

void test_unchanged_best_prices_write_nothing() {
    std::ostringstream os;
    SnapshotLogger log(os);
    log.record(10, 1, Opt(99), Opt(101));
    CHECK(!log.record(20, 2, Opt(99), Opt(101)));
    CHECK(!log.record(30, 3, Opt(99), Opt(101)));
    CHECK(os.str() == kHeader + "10,1,99,101,2\n");
    CHECK(log.rowsWritten() == 1);
}

void test_a_change_of_either_side_writes_a_row() {
    std::ostringstream os;
    SnapshotLogger log(os);
    log.record(10, 1, Opt(99), Opt(101));
    CHECK(log.record(20, 2, Opt(100), Opt(101)));    // bid moved
    CHECK(log.record(30, 3, Opt(100), Opt(102)));    // ask moved
    CHECK(os.str() == kHeader +
                      "10,1,99,101,2\n"
                      "20,2,100,101,1\n"
                      "30,3,100,102,2\n");
}

void test_empty_sides_are_blank_and_spread_needs_both() {
    std::ostringstream os;
    SnapshotLogger log(os);
    log.record(1, 1, std::nullopt, Opt(101));        // no bids yet
    log.record(2, 2, Opt(99), Opt(101));
    log.record(3, 3, Opt(99), std::nullopt);         // asks all gone
    log.record(4, 4, std::nullopt, std::nullopt);    // book empty
    CHECK(os.str() == kHeader +
                      "1,1,,101,\n"
                      "2,2,99,101,2\n"
                      "3,3,99,,\n"
                      "4,4,,,\n");
}

void test_empty_book_at_start_still_writes_the_first_row() {
    std::ostringstream os;
    SnapshotLogger log(os);
    CHECK(log.record(5, 1, std::nullopt, std::nullopt));
    CHECK(!log.record(6, 2, std::nullopt, std::nullopt));
    CHECK(log.rowsWritten() == 1);
}

void test_going_back_to_an_earlier_value_counts_as_a_change() {
    std::ostringstream os;
    SnapshotLogger log(os);
    log.record(1, 1, Opt(99), Opt(101));
    log.record(2, 2, Opt(100), Opt(101));
    CHECK(log.record(3, 3, Opt(99), Opt(101)));      // compares with the LAST row, not the first
    CHECK(log.rowsWritten() == 3);
}

}  // namespace

int main() {
    RUN(test_header_is_written_immediately);
    RUN(test_first_call_always_writes_a_row);
    RUN(test_unchanged_best_prices_write_nothing);
    RUN(test_a_change_of_either_side_writes_a_row);
    RUN(test_empty_sides_are_blank_and_spread_needs_both);
    RUN(test_empty_book_at_start_still_writes_the_first_row);
    RUN(test_going_back_to_an_earlier_value_counts_as_a_change);
    return finish();
}
