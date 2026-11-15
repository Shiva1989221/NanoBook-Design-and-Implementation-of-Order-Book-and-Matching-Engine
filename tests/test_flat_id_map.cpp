#include <cstddef>
#include <cstdint>
#include <iterator>
#include <random>
#include <unordered_map>

#include "check.hpp"
#include "nanobook/flat_id_map.hpp"

using namespace nanobook;

namespace {

void test_insert_find_erase_basics() {
    FlatIdMap<int> map;
    CHECK(map.size() == 0);
    CHECK(map.find(5) == nullptr);

    CHECK(map.insert(5, 50));
    CHECK(map.insert(6, 60));
    CHECK(map.size() == 2);
    CHECK(map.find(5) != nullptr && *map.find(5) == 50);
    CHECK(map.find(6) != nullptr && *map.find(6) == 60);
    CHECK(map.find(7) == nullptr);

    CHECK(map.erase(5));
    CHECK(map.find(5) == nullptr);
    CHECK(map.find(6) != nullptr && *map.find(6) == 60);   // neighbour unaffected
    CHECK(map.size() == 1);
    CHECK(!map.erase(5));                                  // already gone
    CHECK(!map.erase(999));                                // never existed
}

void test_key_zero_is_reserved() {
    FlatIdMap<int> map;
    CHECK(!map.insert(0, 1));
    CHECK(map.size() == 0);
    CHECK(map.find(0) == nullptr);   // must NOT match an empty slot
    CHECK(!map.erase(0));
}

void test_insert_existing_key_overwrites() {
    FlatIdMap<int> map;
    CHECK(map.insert(9, 1));
    CHECK(!map.insert(9, 2));        // not new
    CHECK(map.size() == 1);
    CHECK(map.find(9) != nullptr && *map.find(9) == 2);
}

void test_grows_and_keeps_every_entry() {
    FlatIdMap<std::uint64_t> map;
    const std::uint64_t n = 100000;          // many doublings from the initial 1024
    for (std::uint64_t k = 1; k <= n; ++k) map.insert(k, k * 7);
    CHECK(map.size() == n);
    CHECK(map.capacity() >= 2 * n);          // load factor stays at or below 50%

    bool allThere = true;
    for (std::uint64_t k = 1; k <= n; ++k) {
        const std::uint64_t* v = map.find(k);
        if (v == nullptr || *v != k * 7) allThere = false;
    }
    CHECK(allThere);
    CHECK(map.find(n + 1) == nullptr);
}

void test_erase_then_reinsert() {
    FlatIdMap<int> map;
    for (int k = 1; k <= 5000; ++k) map.insert(static_cast<std::uint64_t>(k), k);
    for (int k = 1; k <= 5000; k += 2) map.erase(static_cast<std::uint64_t>(k));
    CHECK(map.size() == 2500);
    for (int k = 1; k <= 5000; k += 2) map.insert(static_cast<std::uint64_t>(k), -k);
    CHECK(map.size() == 5000);
    bool ok = true;
    for (int k = 1; k <= 5000; ++k) {
        const int* v = map.find(static_cast<std::uint64_t>(k));
        int expected = (k % 2 == 0) ? k : -k;
        if (v == nullptr || *v != expected) ok = false;
    }
    CHECK(ok);
}

// The important one: random inserts/erases/lookups on a SMALL key range (so keys collide and
// clusters form constantly, which is what stresses backward-shift deletion), compared step by
// step with std::unordered_map. We run it with several key ranges.
//
// maxLive > 0 caps how many keys may be stored at once (so a small table never grows) and makes
// erase/lookup mostly target keys that exist (otherwise a wide key range would almost always miss).
void randomized_comparison(std::uint64_t keyRange, int steps, std::uint64_t seed,
                           std::size_t initialCapacity = 1024, std::size_t maxLive = 0) {
    std::mt19937_64 rng(seed);
    FlatIdMap<int> flat(initialCapacity);
    std::unordered_map<std::uint64_t, int> ref;

    for (int step = 0; step < steps; ++step) {
        std::uint64_t key = 1 + rng() % keyRange;
        int op = static_cast<int>(rng() % 3);
        if (maxLive > 0) {
            if (op != 0 && !ref.empty() && rng() % 4 != 0)      // aim at an existing key
                key = std::next(ref.begin(), static_cast<std::ptrdiff_t>(rng() % ref.size()))->first;
            if (op == 0 && ref.size() >= maxLive && ref.find(key) == ref.end())
                continue;                                       // would exceed the cap: skip
        }

        bool same = true;
        if (op == 0) {                                  // insert / overwrite
            bool wasNew = ref.find(key) == ref.end();
            ref[key] = step;
            same = flat.insert(key, step) == wasNew;
        } else if (op == 1) {                           // erase
            same = flat.erase(key) == (ref.erase(key) == 1);
        } else {                                        // lookup
            auto it = ref.find(key);
            const int* v = flat.find(key);
            same = (it == ref.end()) ? (v == nullptr) : (v != nullptr && *v == it->second);
        }
        same = same && flat.size() == ref.size();

        if (!same) {
            CHECK(same);
            std::cerr << "  diverged at step " << step << " (key range " << keyRange << ")\n";
            return;
        }
    }

    bool finalOk = true;                                // full audit at the end
    for (const auto& kv : ref) {
        const int* v = flat.find(kv.first);
        if (v == nullptr || *v != kv.second) finalOk = false;
    }
    CHECK(finalOk);
}

void test_random_operations_match_std_unordered_map() {
    randomized_comparison(64, 200000, 1);          // tiny range: constant collisions
    randomized_comparison(700, 200000, 2);         // around the table's initial load threshold
    randomized_comparison(5000, 300000, 3);        // forces growth during the run
    randomized_comparison(1ull << 40, 100000, 4);  // sparse, wide keys
}

// Clusters that wrap around the END of the array take a different code path in erase().
// With a 1024-slot table that almost never happens, so use tiny tables where it happens
// constantly. (Found by deliberately breaking erase(): the old tests did not notice.)
void test_random_operations_on_tiny_tables_with_wraparound() {
    randomized_comparison(4, 100000, 5, 8);     // at most 4 keys in 8 slots: never grows
    randomized_comparison(12, 100000, 6, 32);
    randomized_comparison(40, 100000, 7, 128);
    randomized_comparison(100, 100000, 8, 2);   // starts tiny and grows repeatedly

    // Wide random keys land on all sorts of home slots; the cap keeps the table from growing,
    // so clusters wrap around the end of the array again and again.
    randomized_comparison(1ull << 40, 200000, 9, 8, 4);
    randomized_comparison(1ull << 40, 200000, 10, 16, 8);
    randomized_comparison(1ull << 40, 200000, 11, 32, 16);
    randomized_comparison(1ull << 40, 200000, 12, 64, 32);
}

}  // namespace

int main() {
    RUN(test_insert_find_erase_basics);
    RUN(test_key_zero_is_reserved);
    RUN(test_insert_existing_key_overwrites);
    RUN(test_grows_and_keeps_every_entry);
    RUN(test_erase_then_reinsert);
    RUN(test_random_operations_match_std_unordered_map);
    RUN(test_random_operations_on_tiny_tables_with_wraparound);
    return finish();
}
