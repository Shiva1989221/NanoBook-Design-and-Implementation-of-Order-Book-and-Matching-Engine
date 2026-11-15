#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>

#include "nanobook/types.hpp"

namespace nanobook {

// One line of the input file.
//   ADD,id,side,type,price,quantity[,timestamp]     e.g. ADD,1,SELL,LIMIT,101,10,5000
//   CANCEL,id[,,,,,timestamp]                       e.g. CANCEL,1
//   MODIFY,id,,,price,quantity[,timestamp]          e.g. MODIFY,2,,,101,6
// The timestamp (last column) is optional; without it the field stays 0 ("not given").
// Blank lines, lines starting with '#', and a header line starting with "action" are skipped.
enum class Action { Add, Cancel, Modify };

struct Command {
    Action    action   = Action::Add;
    OrderId   id       = 0;
    Side      side     = Side::Buy;
    OrderType type     = OrderType::Limit;
    Price     price    = 0;
    Quantity  quantity = 0;
    Timestamp timestamp = 0;   // 0 = not given
};

enum class ParseStatus { Ok, Skipped, Error };

// Parses one line. On Error, `error` says what was wrong.
ParseStatus parseCommand(const std::string& line, Command& cmd, std::string& error);

// Writes one command as an input-file line (the inverse of parseCommand).
void writeCommand(std::ostream& os, const Command& cmd);

// trades.csv: trade_id,buy_order_id,sell_order_id,price,quantity,timestamp
void writeTradeHeader(std::ostream& os);
void writeTrade(std::ostream& os, std::uint64_t tradeId, const Trade& trade);

}  // namespace nanobook
