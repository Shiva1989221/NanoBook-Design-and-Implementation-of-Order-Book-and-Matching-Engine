#pragma once
#include <cstddef>
#include <new>

namespace nanobook {

// A pooling allocator for node-based containers (std::list, std::map, std::unordered_map).
//
// Why: by default every element of those containers is a separate malloc/free call.
// malloc must be general (any size, any thread), so it is slower than it needs to be, and
// nodes end up scattered around memory. Here, all nodes of one type come from big chunks:
//   - allocate():   pop a slot from a free list           (a few instructions)
//   - deallocate(): push the slot back on the free list   (a few instructions)
//   - a freed slot is reused next, so it is probably still in the CPU cache
//   - a fresh chunk hands out slots in address order, so early nodes sit next to each other
//
// How it plugs in: containers rebind the allocator to their own node type, so each node type
// gets its own pool automatically. Requests for n != 1 elements (e.g. the hash table's bucket
// array) are rare and big, so they go to the normal ::operator new.
//
// Limits (fine for this project, worth knowing for the viva):
//   - NOT thread-safe: the pools are global per type. NanoBook is single-threaded.
//   - Memory is recycled but never returned to the OS (the pool keeps its peak size).
//   - The pools are intentionally never destroyed, so an OrderBook that lives in a global or
//     static variable can still safely free its nodes during program exit.
template <typename T>
class PoolAllocator {
    static_assert(alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__,
                  "over-aligned types are not supported by this simple pool");

public:
    using value_type = T;

    PoolAllocator() noexcept = default;
    template <typename U>
    PoolAllocator(const PoolAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        if (n == 1) return pool().allocate();
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }

    void deallocate(T* p, std::size_t n) noexcept {
        if (n == 1) pool().deallocate(p);
        else        ::operator delete(p);
    }

    // All PoolAllocators are interchangeable (the pools are global), so they always compare equal.
    template <typename U>
    bool operator==(const PoolAllocator<U>&) const noexcept { return true; }
    template <typename U>
    bool operator!=(const PoolAllocator<U>&) const noexcept { return false; }

private:
    class Pool {
    public:
        T* allocate() {
            if (freeList_ == nullptr) grow();
            Slot* slot = freeList_;
            freeList_  = slot->next;
            return reinterpret_cast<T*>(slot);
        }

        void deallocate(T* p) noexcept {
            Slot* slot = reinterpret_cast<Slot*>(p);
            slot->next = freeList_;
            freeList_  = slot;
        }

    private:
        // A slot is either a free-list link (when free) or storage for one T (when in use).
        union Slot {
            Slot* next;
            alignas(T) unsigned char storage[sizeof(T)];
        };

        static constexpr std::size_t kSlotsPerChunk = 4096;

        void grow() {
            Slot* chunk = static_cast<Slot*>(::operator new(sizeof(Slot) * kSlotsPerChunk));
            // Link slots so that allocation hands them out in increasing address order.
            for (std::size_t i = kSlotsPerChunk; i-- > 0;) {
                chunk[i].next = freeList_;
                freeList_     = &chunk[i];
            }
        }

        Slot* freeList_ = nullptr;
    };

    static Pool& pool() {
        // Deliberately leaked ("never destroyed"): see the note above about static OrderBooks.
        static Pool* instance = new Pool;
        return *instance;
    }
};

}  // namespace nanobook
