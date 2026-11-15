// NanoBook - command-line program: reads commands from a CSV file, runs them through the
// order book, and writes every trade to another CSV file.
//
// Usage:  nanobook <input.csv> <trades.csv> [--book] [--snapshots <file.csv>]
//   --book             print the final order book
//   --snapshots FILE   also write the best bid/ask history (a row whenever either changes)
//
// Timestamps: an input line may end with a timestamp column. If it does not, the command's
// sequence number (1, 2, 3, ...) is used instead, so the output is always deterministic.

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "nanobook/csv_io.hpp"
#include "nanobook/engine.hpp"
#include "nanobook/order_book.hpp"
#include "nanobook/snapshot_log.hpp"

using namespace nanobook;

namespace {

void printUsage(const char* program) {
    std::cerr << "Usage: " << program
              << " <input.csv> <trades.csv> [--book] [--snapshots <file.csv>]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> paths;   // the two positional arguments
    std::string snapshotPath;
    bool showBook = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--book") {
            showBook = true;
        } else if (arg == "--snapshots") {
            if (i + 1 >= argc) { printUsage(argv[0]); return 1; }
            snapshotPath = argv[++i];
        } else if (arg.rfind("--", 0) == 0) {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        } else {
            paths.push_back(arg);
        }
    }
    if (paths.size() != 2) { printUsage(argv[0]); return 1; }

    std::ifstream in(paths[0]);
    if (!in) {
        std::cerr << "Cannot open input file: " << paths[0] << "\n";
        return 1;
    }
    std::ofstream out(paths[1]);
    if (!out) {
        std::cerr << "Cannot open output file: " << paths[1] << "\n";
        return 1;
    }
    std::ofstream snapshotOut;
    std::unique_ptr<SnapshotLogger> snapshots;
    if (!snapshotPath.empty()) {
        snapshotOut.open(snapshotPath);
        if (!snapshotOut) {
            std::cerr << "Cannot open snapshot file: " << snapshotPath << "\n";
            return 1;
        }
        snapshots = std::make_unique<SnapshotLogger>(snapshotOut);
    }

    writeTradeHeader(out);

    OrderBook book;
    std::vector<Trade> trades;  // reused for every command (clear() keeps its memory)
    std::uint64_t tradeCount = 0, applied = 0, rejected = 0, parseErrors = 0, commandNumber = 0;
    std::size_t lineNo = 0;
    std::string line;

    while (std::getline(in, line)) {
        ++lineNo;

        Command cmd;
        std::string error;
        ParseStatus status = parseCommand(line, cmd, error);
        if (status == ParseStatus::Skipped) continue;
        if (status == ParseStatus::Error) {
            ++parseErrors;
            std::cerr << "line " << lineNo << ": " << error << "\n";
            continue;
        }

        ++commandNumber;
        if (cmd.timestamp == 0) cmd.timestamp = commandNumber;   // fallback: sequence number

        trades.clear();
        bool ok = applyCommand(book, cmd, trades);

        if (ok) {
            ++applied;
        } else {
            ++rejected;
            std::cerr << "line " << lineNo << ": rejected by the order book\n";
        }
        for (const Trade& t : trades) writeTrade(out, ++tradeCount, t);

        if (snapshots) snapshots->record(cmd.timestamp, commandNumber, book.bestBid(), book.bestAsk());
    }

    std::cout << "Commands applied : " << applied << "\n"
              << "Rejected         : " << rejected << "\n"
              << "Parse errors     : " << parseErrors << "\n"
              << "Trades written   : " << tradeCount << "\n"
              << "Resting orders   : " << book.restingOrderCount() << "\n";
    if (snapshots) std::cout << "Snapshot rows    : " << snapshots->rowsWritten() << "\n";

    auto bid = book.bestBid();
    auto ask = book.bestAsk();
    std::cout << "Best bid         : ";
    if (bid) std::cout << *bid; else std::cout << "-";
    std::cout << "\nBest ask         : ";
    if (ask) std::cout << *ask; else std::cout << "-";
    std::cout << "\n";

    if (showBook) {
        std::cout << "\nBook:\n";
        book.print(std::cout);
    }
    return 0;
}
