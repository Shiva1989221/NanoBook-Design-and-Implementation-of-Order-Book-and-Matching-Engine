#include "nanobook/csv_io.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <ostream>
#include <sstream>
#include <system_error>
#include <vector>

namespace nanobook {

namespace {  // helpers visible only inside this file

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    auto first = s.find_first_not_of(ws);
    if (first == std::string::npos) return "";
    auto last = s.find_last_not_of(ws);
    return s.substr(first, last - first + 1);
}

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) fields.push_back(trim(field));
    return fields;
}

// std::from_chars: fast, no exceptions, no locale. Rejects "-5" for unsigned types,
// rejects overflow, and we also require that the WHOLE string was consumed.
template <typename T>
bool parseInteger(const std::string& s, T& out) {
    if (s.empty()) return false;
    const char* first = s.data();
    const char* last  = first + s.size();
    auto result = std::from_chars(first, last, out);
    return result.ec == std::errc() && result.ptr == last;
}

}  // namespace

ParseStatus parseCommand(const std::string& rawLine, Command& cmd, std::string& error) {
    std::string line = trim(rawLine);
    if (line.empty() || line[0] == '#') return ParseStatus::Skipped;

    std::vector<std::string> f = split(line);
    auto field = [&](std::size_t i) { return i < f.size() ? f[i] : std::string(); };

    std::string action = toUpper(field(0));
    if (action == "ACTION") return ParseStatus::Skipped;  // header row

    cmd = Command{};
    if (!parseInteger(field(1), cmd.id)) {
        error = "invalid or missing order id";
        return ParseStatus::Error;
    }

    // Optional last column, valid for every action.
    const std::string ts = field(6);
    if (!ts.empty() && !parseInteger(ts, cmd.timestamp)) {
        error = "invalid timestamp";
        return ParseStatus::Error;
    }

    if (action == "CANCEL") {
        cmd.action = Action::Cancel;
        return ParseStatus::Ok;
    }

    if (action == "ADD") {
        cmd.action = Action::Add;

        std::string side = toUpper(field(2));
        if      (side == "BUY")  cmd.side = Side::Buy;
        else if (side == "SELL") cmd.side = Side::Sell;
        else { error = "side must be BUY or SELL"; return ParseStatus::Error; }

        std::string type = toUpper(field(3));
        if      (type == "LIMIT")  cmd.type = OrderType::Limit;
        else if (type == "MARKET") cmd.type = OrderType::Market;
        else { error = "type must be LIMIT or MARKET"; return ParseStatus::Error; }

        // Market orders ignore price, so it may be blank.
        if (cmd.type == OrderType::Limit && !parseInteger(field(4), cmd.price)) {
            error = "invalid or missing price";
            return ParseStatus::Error;
        }
        if (!parseInteger(field(5), cmd.quantity)) {
            error = "invalid or missing quantity";
            return ParseStatus::Error;
        }
        return ParseStatus::Ok;
    }

    if (action == "MODIFY") {
        cmd.action = Action::Modify;
        if (!parseInteger(field(4), cmd.price)) {
            error = "invalid or missing price";
            return ParseStatus::Error;
        }
        if (!parseInteger(field(5), cmd.quantity)) {
            error = "invalid or missing quantity";
            return ParseStatus::Error;
        }
        return ParseStatus::Ok;
    }

    error = "unknown action '" + field(0) + "' (expected ADD, CANCEL or MODIFY)";
    return ParseStatus::Error;
}

void writeCommand(std::ostream& os, const Command& c) {
    switch (c.action) {
        case Action::Add:
            os << "ADD," << c.id << ',' << (c.side == Side::Buy ? "BUY" : "SELL") << ','
               << (c.type == OrderType::Limit ? "LIMIT" : "MARKET") << ',';
            if (c.type == OrderType::Limit) os << c.price;   // market orders have no price
            os << ',' << c.quantity;
            if (c.timestamp != 0) os << ',' << c.timestamp;
            os << '\n';
            break;
        case Action::Cancel:
            os << "CANCEL," << c.id;
            if (c.timestamp != 0) os << ",,,,," << c.timestamp;   // 4 empty columns, then timestamp
            os << '\n';
            break;
        case Action::Modify:
            os << "MODIFY," << c.id << ",,," << c.price << ',' << c.quantity;
            if (c.timestamp != 0) os << ',' << c.timestamp;
            os << '\n';
            break;
    }
}

void writeTradeHeader(std::ostream& os) {
    os << "trade_id,buy_order_id,sell_order_id,price,quantity,timestamp\n";
}

void writeTrade(std::ostream& os, std::uint64_t tradeId, const Trade& t) {
    // '\n' instead of std::endl: endl flushes the stream every time, which is very slow.
    os << tradeId << ',' << t.buyOrderId << ',' << t.sellOrderId << ','
       << t.price << ',' << t.quantity << ',' << t.timestamp << '\n';
}

}  // namespace nanobook
