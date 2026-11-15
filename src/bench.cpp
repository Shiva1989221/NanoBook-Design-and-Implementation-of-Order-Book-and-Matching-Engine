// NanoBook - Step 5: benchmark.
//
// Usage:  nanobook_bench [num_commands] [--seed N] [--runs N] [--csv workload.csv]
//   num_commands  how many synthetic commands to generate (default 1000000)
//   --seed N      random seed (default 42); same seed = same workload
//   --runs N      number of timed throughput runs (default 3)
//   --csv FILE    also save the workload as a CSV that `nanobook` can replay
//
// Two kinds of measurement, deliberately separate:
//   1. Throughput: time the whole loop with ONE clock reading before and after.
//      No per-command timing overhead, so this is the honest orders/second number.
//   2. Latency: read the clock around EVERY command. Reading the clock costs time
//      too (reported below), so absolute latencies include that overhead.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "nanobook/engine.hpp"
#include "nanobook/workload.hpp"

using namespace nanobook;
using Clock = std::chrono::steady_clock;

namespace {

struct Options {
    std::size_t   count = 1000000;
    std::uint64_t seed  = 42;
    int           runs  = 3;
    std::string   csvPath;
};

bool parseUnsigned(const char* text, std::uint64_t& out) {
    char* end = nullptr;
    out = std::strtoull(text, &end, 10);
    return end != text && *end == '\0';
}

bool parseArgs(int argc, char** argv, Options& opt) {
    bool haveCount = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::uint64_t value = 0;
        if (arg == "--seed" || arg == "--runs" || arg == "--csv") {
            if (i + 1 >= argc) return false;
            const char* next = argv[++i];
            if (arg == "--csv") {
                opt.csvPath = next;
            } else if (!parseUnsigned(next, value)) {
                return false;
            } else if (arg == "--seed") {
                opt.seed = value;
            } else {
                opt.runs = static_cast<int>(value);
            }
        } else if (!haveCount && parseUnsigned(argv[i], value)) {
            opt.count = static_cast<std::size_t>(value);
            haveCount = true;
        } else {
            return false;
        }
    }
    return opt.count > 0 && opt.runs > 0;
}

// ---------- throughput ----------

struct RunResult {
    double               seconds  = 0;
    std::uint64_t        trades   = 0;
    std::uint64_t        rejected = 0;
    std::size_t          resting  = 0;
    std::optional<Price> bestBid;
    std::optional<Price> bestAsk;
};

RunResult runOnce(const std::vector<Command>& commands) {
    OrderBook book;
    std::vector<Trade> trades;   // reused: clear() keeps its memory
    RunResult r;

    auto start = Clock::now();
    for (const Command& c : commands) {
        trades.clear();
        if (!applyCommand(book, c, trades)) ++r.rejected;
        r.trades += trades.size();
    }
    auto stop = Clock::now();

    r.seconds = std::chrono::duration<double>(stop - start).count();
    r.resting = book.restingOrderCount();
    r.bestBid = book.bestBid();
    r.bestAsk = book.bestAsk();
    return r;   // the book is destroyed here, AFTER the clock stopped
}

// ---------- latency ----------

enum Category { AddNoTrade, AddWithTrade, CancelOrder, ModifyOrder, kCategories };

const char* categoryName(int c) {
    switch (c) {
        case AddNoTrade:   return "add (rests)";
        case AddWithTrade: return "add (trades)";
        case CancelOrder:  return "cancel";
        default:           return "modify";
    }
}

struct Sample {
    std::uint32_t ns;
    std::uint8_t  category;
};

std::uint64_t percentile(const std::vector<std::uint32_t>& sorted, double p) {
    // Nearest-rank method: the smallest value with at least p% of samples at or below it.
    std::size_t rank = static_cast<std::size_t>(p / 100.0 * static_cast<double>(sorted.size()) + 0.999999);
    rank = std::max<std::size_t>(1, std::min(rank, sorted.size()));
    return sorted[rank - 1];
}

void printLatencyRow(const std::string& name, std::vector<std::uint32_t> values) {
    if (values.empty()) return;
    std::sort(values.begin(), values.end());
    double sum = 0;
    for (std::uint32_t v : values) sum += v;
    std::cout << std::left << std::setw(14) << name << std::right
              << std::setw(10) << values.size()
              << std::setw(9)  << static_cast<std::uint64_t>(sum / static_cast<double>(values.size()))
              << std::setw(9)  << percentile(values, 50)
              << std::setw(9)  << percentile(values, 90)
              << std::setw(9)  << percentile(values, 99)
              << std::setw(9)  << percentile(values, 99.9)
              << std::setw(10) << values.back() << "\n";
}

double measureTimerOverheadNs() {
    const int n = 200000;
    std::uint64_t total = 0;
    for (int i = 0; i < n; ++i) {
        auto a = Clock::now();
        auto b = Clock::now();
        total += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
    }
    return static_cast<double>(total) / n;
}

void runLatency(const std::vector<Command>& commands) {
    OrderBook book;
    std::vector<Trade> trades;
    std::vector<Sample> samples;
    samples.reserve(commands.size());   // no allocation while timing

    for (const Command& c : commands) {
        trades.clear();
        auto t0 = Clock::now();
        applyCommand(book, c, trades);
        auto t1 = Clock::now();

        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        Sample s;
        s.ns = static_cast<std::uint32_t>(std::min<long long>(ns, 0xFFFFFFFFLL));
        if      (c.action == Action::Cancel) s.category = CancelOrder;
        else if (c.action == Action::Modify) s.category = ModifyOrder;
        else                                 s.category = trades.empty() ? AddNoTrade : AddWithTrade;
        samples.push_back(s);
    }

    std::cout << "\nLatency per command, in nanoseconds (each command timed individually)\n"
              << "  clock-reading overhead is about " << std::fixed << std::setprecision(0)
              << measureTimerOverheadNs() << " ns and is included in every number below\n\n";
    std::cout << std::left << std::setw(14) << "type" << std::right
              << std::setw(10) << "count" << std::setw(9) << "avg" << std::setw(9) << "p50"
              << std::setw(9) << "p90" << std::setw(9) << "p99" << std::setw(9) << "p99.9"
              << std::setw(10) << "max" << "\n";

    std::vector<std::uint32_t> byCategory[kCategories];
    std::vector<std::uint32_t> all;
    all.reserve(samples.size());
    for (const Sample& s : samples) {
        byCategory[s.category].push_back(s.ns);
        all.push_back(s.ns);
    }
    printLatencyRow("ALL", all);
    for (int c = 0; c < kCategories; ++c) printLatencyRow(categoryName(c), byCategory[c]);
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) {
        std::cerr << "Usage: " << argv[0]
                  << " [num_commands] [--seed N] [--runs N] [--csv workload.csv]\n";
        return 1;
    }

    std::cout << "NanoBook benchmark\n"
              << "  commands : " << opt.count << "   seed : " << opt.seed
              << "   timed runs : " << opt.runs << "\n";

    std::vector<Command> commands = generateWorkload(opt.count, opt.seed);

    std::uint64_t limitAdds = 0, marketAdds = 0, cancels = 0, modifies = 0;
    for (const Command& c : commands) {
        if      (c.action == Action::Cancel) ++cancels;
        else if (c.action == Action::Modify) ++modifies;
        else if (c.type == OrderType::Market) ++marketAdds;
        else ++limitAdds;
    }
    std::cout << "  mix      : " << limitAdds << " limit, " << marketAdds << " market, "
              << cancels << " cancel, " << modifies << " modify\n";

    if (!opt.csvPath.empty()) {
        std::ofstream out(opt.csvPath);
        if (!out) {
            std::cerr << "Cannot open " << opt.csvPath << " for writing\n";
            return 1;
        }
        for (const Command& c : commands) writeCommand(out, c);
        std::cout << "  workload saved to " << opt.csvPath << " (replay it with: nanobook "
                  << opt.csvPath << " trades.csv)\n";
    }

    // Warm-up: first run pays for page faults, cold caches and CPU frequency ramp-up.
    runOnce(commands);

    std::cout << "\nThroughput (whole loop timed once; warm-up run not reported)\n";
    std::vector<double> rates;
    RunResult last;
    for (int i = 1; i <= opt.runs; ++i) {
        last = runOnce(commands);
        double rate = static_cast<double>(opt.count) / last.seconds;
        rates.push_back(rate);
        std::cout << "  run " << i << ": " << std::fixed << std::setprecision(3) << last.seconds
                  << " s   " << std::setprecision(2) << rate / 1e6 << " M commands/s   "
                  << std::setprecision(0) << 1e9 / rate << " ns/command\n";
    }
    std::sort(rates.begin(), rates.end());
    double median = rates[rates.size() / 2];
    std::cout << "  median: " << std::setprecision(2) << median / 1e6 << " M commands/s\n";

    std::cout << "\nState after the workload\n"
              << "  trades      : " << last.trades << "\n"
              << "  rejected    : " << last.rejected << "\n"
              << "  resting     : " << last.resting << "\n"
              << "  best bid/ask: ";
    if (last.bestBid) std::cout << *last.bestBid; else std::cout << "-";
    std::cout << " / ";
    if (last.bestAsk) std::cout << *last.bestAsk; else std::cout << "-";
    std::cout << "\n";

    runLatency(commands);
    return 0;
}
