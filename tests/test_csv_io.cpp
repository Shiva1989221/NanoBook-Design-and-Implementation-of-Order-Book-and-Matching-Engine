#include <sstream>
#include <string>

#include "check.hpp"
#include "nanobook/csv_io.hpp"

using namespace nanobook;

namespace {

ParseStatus parse(const std::string& line, Command& cmd, std::string& error) {
    return parseCommand(line, cmd, error);
}

bool parsesOk(const std::string& line, Command& cmd) {
    std::string error;
    return parse(line, cmd, error) == ParseStatus::Ok;
}

// True if the line is reported as an Error WITH a non-empty message.
bool isError(const std::string& line) {
    Command cmd;
    std::string error;
    return parse(line, cmd, error) == ParseStatus::Error && !error.empty();
}

bool isSkipped(const std::string& line) {
    Command cmd;
    std::string error;
    return parse(line, cmd, error) == ParseStatus::Skipped;
}

void test_parse_add_limit() {
    Command c;
    CHECK(parsesOk("ADD,1,SELL,LIMIT,101,10", c));
    CHECK(c.action == Action::Add);
    CHECK(c.id == 1);
    CHECK(c.side == Side::Sell);
    CHECK(c.type == OrderType::Limit);
    CHECK(c.price == 101);
    CHECK(c.quantity == 10);
}

void test_parse_add_market_with_blank_price() {
    Command c;
    CHECK(parsesOk("ADD,9,BUY,MARKET,,8", c));
    CHECK(c.action == Action::Add);
    CHECK(c.side == Side::Buy);
    CHECK(c.type == OrderType::Market);
    CHECK(c.price == 0);
    CHECK(c.quantity == 8);
}

void test_parse_cancel() {
    Command c;
    CHECK(parsesOk("CANCEL,5", c));
    CHECK(c.action == Action::Cancel);
    CHECK(c.id == 5);
    CHECK(parsesOk("CANCEL,6,,,,", c));    // trailing empty fields are fine
    CHECK(c.id == 6);
}

void test_parse_modify() {
    Command c;
    CHECK(parsesOk("MODIFY,2,,,101,6", c));
    CHECK(c.action == Action::Modify);
    CHECK(c.id == 2);
    CHECK(c.price == 101);
    CHECK(c.quantity == 6);
}

void test_parse_ignores_case_and_spaces() {
    Command c;
    CHECK(parsesOk("  add , 3 , buy , limit , 100 , 7  ", c));
    CHECK(c.id == 3 && c.side == Side::Buy && c.type == OrderType::Limit);
    CHECK(c.price == 100 && c.quantity == 7);
}

void test_parse_handles_windows_line_endings() {
    Command c;
    CHECK(parsesOk("ADD,1,SELL,LIMIT,101,10\r", c));
    CHECK(c.quantity == 10);
}

void test_skipped_lines() {
    CHECK(isSkipped(""));
    CHECK(isSkipped("    "));
    CHECK(isSkipped("\r"));
    CHECK(isSkipped("# a comment"));
    CHECK(isSkipped("action,id,side,type,price,quantity"));   // header row
}

void test_parse_optional_timestamp() {
    Command c;
    CHECK(parsesOk("ADD,1,SELL,LIMIT,101,10,5000", c));
    CHECK(c.timestamp == 5000 && c.quantity == 10);
    CHECK(parsesOk("ADD,9,BUY,MARKET,,8,6000", c));
    CHECK(c.timestamp == 6000 && c.price == 0);
    CHECK(parsesOk("CANCEL,5,,,,,7000", c));
    CHECK(c.action == Action::Cancel && c.id == 5 && c.timestamp == 7000);
    CHECK(parsesOk("MODIFY,2,,,101,6,8000", c));
    CHECK(c.action == Action::Modify && c.timestamp == 8000);

    CHECK(parsesOk("ADD,1,SELL,LIMIT,101,10", c));    // no timestamp column at all
    CHECK(c.timestamp == 0);                          // 0 = "not given"
    CHECK(parsesOk("ADD,1,SELL,LIMIT,101,10,", c));   // empty timestamp column
    CHECK(c.timestamp == 0);
}

void test_parse_timestamp_errors() {
    CHECK(isError("ADD,1,BUY,LIMIT,100,5,abc"));
    CHECK(isError("ADD,1,BUY,LIMIT,100,5,-3"));
    CHECK(isError("CANCEL,5,,,,,12x"));
    CHECK(isError("MODIFY,2,,,101,6,99999999999999999999999"));   // does not fit in 64 bits
}

void test_parse_errors() {
    CHECK(isError("FLY,7"));                        // unknown action
    CHECK(isError("ADD,,BUY,LIMIT,100,5"));         // missing id
    CHECK(isError("ADD,-1,BUY,LIMIT,100,5"));       // negative id
    CHECK(isError("ADD,abc,BUY,LIMIT,100,5"));      // non-numeric id
    CHECK(isError("ADD,1,HOLD,LIMIT,100,5"));       // bad side
    CHECK(isError("ADD,1,BUY,STOP,100,5"));         // bad type
    CHECK(isError("ADD,1,BUY,LIMIT,abc,5"));        // non-numeric price
    CHECK(isError("ADD,1,BUY,LIMIT,10x,5"));        // trailing junk in a number
    CHECK(isError("ADD,1,BUY,LIMIT,,5"));           // limit order needs a price
    CHECK(isError("ADD,1,BUY,LIMIT,100,-5"));       // negative quantity
    CHECK(isError("ADD,1,BUY,LIMIT,100,99999999999"));  // quantity overflows 32 bits
    CHECK(isError("ADD,1,BUY,LIMIT,100"));          // missing quantity
    CHECK(isError("MODIFY,2,,,,6"));                // modify needs a price
    CHECK(isError("MODIFY,2,,,101"));               // modify needs a quantity
}

void test_parser_leaves_business_rules_to_the_order_book() {
    // A negative price is a well-formed number, so the PARSER accepts it;
    // OrderBook::addOrder is what rejects it (tested in test_order_book).
    Command c;
    CHECK(parsesOk("ADD,1,BUY,LIMIT,-5,5", c));
    CHECK(c.price == -5);
}

void test_write_trade_format() {
    std::ostringstream os;
    writeTradeHeader(os);
    writeTrade(os, 1, Trade{7, 6, 101, 4, 5000});
    writeTrade(os, 2, Trade{4, 3, 102, 5, 5100});
    CHECK(os.str() ==
          "trade_id,buy_order_id,sell_order_id,price,quantity,timestamp\n"
          "1,7,6,101,4,5000\n"
          "2,4,3,102,5,5100\n");
}

}  // namespace

int main() {
    RUN(test_parse_add_limit);
    RUN(test_parse_add_market_with_blank_price);
    RUN(test_parse_cancel);
    RUN(test_parse_modify);
    RUN(test_parse_ignores_case_and_spaces);
    RUN(test_parse_handles_windows_line_endings);
    RUN(test_skipped_lines);
    RUN(test_parse_optional_timestamp);
    RUN(test_parse_timestamp_errors);
    RUN(test_parse_errors);
    RUN(test_parser_leaves_business_rules_to_the_order_book);
    RUN(test_write_trade_format);
    return finish();
}
