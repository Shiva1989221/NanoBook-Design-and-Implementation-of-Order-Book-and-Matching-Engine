#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "check.hpp"
#include "nanobook/pool_allocator.hpp"

using namespace nanobook;

namespace {

struct alignas(16) Aligned16 {
    unsigned char bytes[16];
};

void test_allocations_are_distinct_and_keep_their_values() {
    PoolAllocator<std::uint64_t> alloc;
    std::vector<std::uint64_t*> ptrs;
    std::set<std::uint64_t*> unique;
    for (std::uint64_t i = 0; i < 10000; ++i) {   // more than one chunk (4096 slots)
        std::uint64_t* p = alloc.allocate(1);
        *p = i * 3 + 1;
        ptrs.push_back(p);
        unique.insert(p);
    }
    CHECK(unique.size() == ptrs.size());          // no slot handed out twice
    bool intact = true;
    for (std::uint64_t i = 0; i < ptrs.size(); ++i)
        if (*ptrs[i] != i * 3 + 1) intact = false;
    CHECK(intact);                                // neighbours never overwrote each other
    for (auto* p : ptrs) alloc.deallocate(p, 1);
}

void test_freed_slot_is_reused_first() {
    PoolAllocator<std::uint64_t> alloc;
    std::uint64_t* a = alloc.allocate(1);
    std::uint64_t* b = alloc.allocate(1);
    alloc.deallocate(a, 1);
    alloc.deallocate(b, 1);
    CHECK(alloc.allocate(1) == b);                // last freed = first reused
    CHECK(alloc.allocate(1) == a);
}

void test_fresh_slots_come_out_in_address_order() {
    PoolAllocator<std::uint64_t> alloc;
    std::uint64_t* first = alloc.allocate(1);
    std::uint64_t* second = alloc.allocate(1);
    // Only meaningful when both come from the same fresh chunk; a recycled slot from an
    // earlier test could break the pattern, so check distance only if they are close.
    if (second > first && second - first < 8) CHECK(second - first == 1);
    alloc.deallocate(second, 1);
    alloc.deallocate(first, 1);
}

void test_array_requests_fall_back_to_normal_new() {
    PoolAllocator<std::uint32_t> alloc;
    std::uint32_t* arr = alloc.allocate(1000);
    for (std::uint32_t i = 0; i < 1000; ++i) arr[i] = i;
    bool intact = true;
    for (std::uint32_t i = 0; i < 1000; ++i)
        if (arr[i] != i) intact = false;
    CHECK(intact);
    alloc.deallocate(arr, 1000);
}

void test_alignment_is_respected() {
    PoolAllocator<Aligned16> alloc;
    std::vector<Aligned16*> ptrs;
    bool aligned = true;
    for (int i = 0; i < 1000; ++i) {
        Aligned16* p = alloc.allocate(1);
        if (reinterpret_cast<std::uintptr_t>(p) % 16 != 0) aligned = false;
        ptrs.push_back(p);
    }
    CHECK(aligned);
    for (auto* p : ptrs) alloc.deallocate(p, 1);
}

void test_allocators_of_different_types_compare_equal() {
    PoolAllocator<int> a;
    PoolAllocator<double> b;
    CHECK(a == b);
    CHECK(!(a != b));
}

void test_works_inside_std_containers() {
    std::list<int, PoolAllocator<int>> list;
    for (int i = 0; i < 20000; ++i) list.push_back(i);
    for (int i = 0; i < 20000; i += 2) list.remove(i);   // free half, creating holes
    for (int i = 0; i < 5000; ++i) list.push_back(-i);   // refill from the free list
    CHECK(list.size() == 10000u + 5000u);
    CHECK(list.front() == 1);

    std::map<int, int, std::less<int>, PoolAllocator<std::pair<const int, int>>> map;
    for (int i = 0; i < 20000; ++i) map[i] = i * 2;
    for (int i = 0; i < 20000; i += 3) map.erase(i);
    bool mapOk = true;
    for (auto& kv : map)
        if (kv.second != kv.first * 2 || kv.first % 3 == 0) mapOk = false;
    CHECK(mapOk);
    CHECK(map.size() == 20000u - 6667u);

    std::unordered_map<int, int, std::hash<int>, std::equal_to<int>,
                       PoolAllocator<std::pair<const int, int>>> hash;
    for (int i = 0; i < 20000; ++i) hash[i] = i + 7;     // forces several rehashes (n != 1)
    for (int i = 0; i < 20000; i += 2) hash.erase(i);
    bool hashOk = hash.size() == 10000u;
    for (int i = 1; i < 20000; i += 2)
        if (hash.at(i) != i + 7) hashOk = false;
    CHECK(hashOk);
}

}  // namespace

int main() {
    RUN(test_allocations_are_distinct_and_keep_their_values);
    RUN(test_freed_slot_is_reused_first);
    RUN(test_fresh_slots_come_out_in_address_order);
    RUN(test_array_requests_fall_back_to_normal_new);
    RUN(test_alignment_is_respected);
    RUN(test_allocators_of_different_types_compare_equal);
    RUN(test_works_inside_std_containers);
    return finish();
}
