#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nanobook {

// A hash map from non-zero 64-bit IDs to values, stored in ONE contiguous array.
//
// Compared with std::unordered_map (a bucket array + one separately allocated node per entry):
//   - a lookup usually touches ONE slot in ONE array, instead of bucket -> node (two jumps)
//   - no allocation per insert/erase
//   - the slot is picked with a multiply and a shift, not an integer division
//
// How it works ("open addressing with linear probing"):
//   - capacity is a power of two; every key has a "home" slot computed from its hash
//   - if the home slot is taken by another key, try the next slot, then the next, ... (wrapping)
//   - a lookup walks from the home slot until it finds the key or hits an empty slot
//   - the table is kept at most half full, so those walks stay short
//
// Deleting is the tricky part. Just emptying a slot would break other keys' probe walks (a
// walk stops at the first empty slot), so erase() uses "backward-shift deletion": it moves
// later entries of the same cluster back into the hole when that keeps them reachable.
//
// Rules / limits:
//   - key 0 is reserved to mark an empty slot (the order book rejects order id 0)
//   - pointers returned by find() are invalidated by any insert() or erase()
//   - growing (rehashing) happens inside insert(); that one call is slower, which can show
//     up as a latency spike. Starting bigger or reserving up front would avoid it.
template <typename V>
class FlatIdMap {
public:
    // `initialCapacity` is rounded up to a power of two (minimum 2). The default is fine for
    // real use; tests pass a tiny value so that clusters wrap around the end of the table often.
    explicit FlatIdMap(std::size_t initialCapacity = kInitialCapacity) {
        std::size_t capacity = 2;
        while (capacity < initialCapacity) capacity *= 2;
        resize(capacity);
    }

    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return slots_.size(); }

    V* find(std::uint64_t key) noexcept {
        std::size_t i = findIndex(key);
        return i == kNotFound ? nullptr : &slots_[i].value;
    }

    const V* find(std::uint64_t key) const noexcept {
        std::size_t i = findIndex(key);
        return i == kNotFound ? nullptr : &slots_[i].value;
    }

    // Inserts the key, or overwrites its value if it already exists.
    // Returns true if the key was new. Key 0 is rejected (returns false, stores nothing).
    bool insert(std::uint64_t key, const V& value) {
        if (key == kEmptyKey) return false;
        if ((size_ + 1) * 2 > slots_.size()) resize(slots_.size() * 2);  // keep load <= 50%

        for (std::size_t i = home(key);; i = next(i)) {
            Slot& s = slots_[i];
            if (s.key == key) {
                s.value = value;
                return false;
            }
            if (s.key == kEmptyKey) {
                s.key   = key;
                s.value = value;
                ++size_;
                return true;
            }
        }
    }

    // Returns true if the key existed and was removed.
    bool erase(std::uint64_t key) noexcept {
        std::size_t hole = findIndex(key);
        if (hole == kNotFound) return false;

        // Backward-shift deletion: walk the rest of the cluster after the hole.
        std::size_t j = hole;
        for (;;) {
            j = next(j);
            if (slots_[j].key == kEmptyKey) break;            // end of the cluster

            std::size_t h = home(slots_[j].key);
            // The entry at j can move into the hole only if its home slot is NOT cyclically
            // inside (hole, j]; otherwise a lookup starting at its home would stop at the hole.
            bool homeIsAfterHoleUpToJ = (hole <= j) ? (hole < h && h <= j)
                                                    : (hole < h || h <= j);
            if (homeIsAfterHoleUpToJ) continue;

            slots_[hole] = slots_[j];
            hole = j;                                         // the hole moves forward
        }
        slots_[hole] = Slot{};
        --size_;
        return true;
    }

private:
    struct Slot {
        std::uint64_t key = kEmptyKey;
        V             value{};
    };

    static constexpr std::uint64_t kEmptyKey = 0;
    static constexpr std::size_t   kNotFound = static_cast<std::size_t>(-1);
    static constexpr std::size_t   kInitialCapacity = 1024;   // must be a power of two

    std::size_t next(std::size_t i) const noexcept { return (i + 1) & mask_; }

    // Fibonacci hashing: multiply by 2^64 / golden ratio and keep the TOP bits. This spreads
    // even patterned keys (1, 2, 3, ... or multiples of 1024) evenly over the table.
    std::size_t home(std::uint64_t key) const noexcept {
        return static_cast<std::size_t>((key * 0x9E3779B97F4A7C15ull) >> shift_);
    }

    std::size_t findIndex(std::uint64_t key) const noexcept {
        if (key == kEmptyKey) return kNotFound;
        for (std::size_t i = home(key);; i = next(i)) {
            if (slots_[i].key == key) return i;
            if (slots_[i].key == kEmptyKey) return kNotFound;
        }
    }

    void resize(std::size_t newCapacity) {
        std::vector<Slot> old;
        old.swap(slots_);
        slots_.assign(newCapacity, Slot{});
        mask_ = newCapacity - 1;

        unsigned bits = 0;
        while ((std::size_t{1} << bits) < newCapacity) ++bits;
        shift_ = 64 - bits;

        for (const Slot& s : old) {
            if (s.key == kEmptyKey) continue;
            std::size_t i = home(s.key);
            while (slots_[i].key != kEmptyKey) i = next(i);
            slots_[i] = s;
        }
    }

    std::vector<Slot> slots_;
    std::size_t       mask_ = 0;
    unsigned          shift_ = 0;
    std::size_t       size_ = 0;
};

}  // namespace nanobook
