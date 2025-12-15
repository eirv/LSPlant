/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

module;

#include <malloc.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <mutex>

#ifdef LSPLANT_USE_MODULES
export module lsplant:arena_allocator;

import :common;
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#pragma ide diagnostic ignored "modernize-use-equals-delete"
#pragma ide diagnostic ignored "misc-non-private-member-variables-in-classes"
#pragma ide diagnostic ignored "misc-include-cleaner"
#pragma ide diagnostic ignored "google-readability-todo"

export namespace lsplant::art::base {

constexpr size_t kArenaDefaultSize = 64ZU * 1024;

class Arena;
class MemMapArenaPool;
template <class ArenaPool>
class ArenaAllocator;

class Arena {
public:
    Arena() = default;

    Arena(uint8_t *memory, size_t size) : memory_(memory), size_(size) {}

    virtual ~Arena() = default;

    // Reset is for pre-use and uses memset for performance.
    void Reset() {
        if (bytes_allocated_ > 0) {
            std::memset(Begin(), 0, bytes_allocated_);
            bytes_allocated_ = 0;
        }
    }

    // Release is used inbetween uses and uses madvise for memory usage.
    virtual void Release() {}

    [[nodiscard]] uint8_t *Begin() const { return memory_; }

    [[nodiscard]] uint8_t *End() const { return memory_ + size_; }

    [[nodiscard]] size_t Size() const { return size_; }

    [[nodiscard]] size_t RemainingSpace() const { return Size() - bytes_allocated_; }

    [[nodiscard]] size_t GetBytesAllocated() const { return bytes_allocated_; }

    // Return true if ptr is contained in the arena.
    [[nodiscard]] bool Contains(const void *ptr) const {
        return memory_ <= ptr && ptr < memory_ + size_;
    }

    [[nodiscard]] Arena *Next() const { return next_; }

protected:
    size_t bytes_allocated_{0};
    uint8_t *memory_{nullptr};
    size_t size_{0};
    Arena *next_{nullptr};

private:
    Arena(const Arena &) = delete;
    void operator=(const Arena &) = delete;

    template <class>
    friend class ArenaAllocator;
    friend class MemMapArenaPool;
};

// Fast single-threaded allocator for zero-initialized memory chunks.
//
// Memory is allocated from ArenaPool in large chunks and then rationed through
// the ArenaAllocator. It's returned to the ArenaPool only when the ArenaAllocator
// is destroyed.
template <class ArenaPool>
class ArenaAllocator {
public:
    template <typename... Args>
    explicit ArenaAllocator(Args... args) : pool_{std::forward<Args>(args)...} {}

    ~ArenaAllocator() {
        // Reclaim all the arenas by giving them back to the thread pool.
        UpdateBytesAllocated();
        pool_.FreeArenaChain(arena_head_);
    }

    // Returns zeroed memory.
    void *Alloc(size_t bytes) {
        bytes = __builtin_align_up(bytes, kAlignment);
        if (bytes > static_cast<size_t>(end_ - ptr_)) [[unlikely]] {
            auto memory = AllocFromNewArena(bytes);
            if (memory) [[likely]] {
                return memory;
            }
            return malloc(bytes);
        }
        uint8_t *ret = ptr_;
        // DCHECK_ALIGNED(ret, kAlignment);
        ptr_ += bytes;
        return ret;
    }

    // Returns zeroed memory.
    void *AllocAlign16(size_t bytes) {
        // It is an error to request 16-byte aligned allocation of unaligned size.
        // DCHECK_ALIGNED(bytes, 16);
        uintptr_t const padding = __builtin_align_up(reinterpret_cast<uintptr_t>(ptr_), 16) -
                                  reinterpret_cast<uintptr_t>(ptr_);
        // ArenaAllocatorStats::RecordAlloc(bytes, kind);
        if (padding + bytes > static_cast<size_t>(end_ - ptr_)) [[unlikely]] {
            auto memory = AllocFromNewArena(bytes);
            if (memory) [[likely]] {
                return memory;
            }
            return memalign(16, bytes);
        }
        ptr_ += padding;
        uint8_t *ret = ptr_;
        // DCHECK_ALIGNED(ret, 16);
        ptr_ += bytes;
        return ret;
    }

    // Realloc never frees the input pointer, it is the caller's job to do this if necessary.
    void *Realloc(void *ptr, size_t ptr_size, size_t new_size) {
        // DCHECK_GE(new_size, ptr_size);
        // DCHECK_EQ(ptr == nullptr, ptr_size == 0u);
        // We always allocate aligned.
        const size_t aligned_ptr_size = __builtin_align_up(ptr_size, kAlignment);
        auto *end = reinterpret_cast<uint8_t *>(ptr) + aligned_ptr_size;
        // If we haven't allocated anything else, we can safely extend.
        if (end == ptr_) {
            // Red zone prevents end == ptr_ (unless input = allocator state = null).
            // DCHECK(!IsRunningOnMemoryTool() || ptr_ == nullptr);
            const size_t aligned_new_size = __builtin_align_up(new_size, kAlignment);
            const size_t size_delta = aligned_new_size - aligned_ptr_size;
            // Check remain space.
            const size_t remain = end_ - ptr_;
            if (remain >= size_delta) {
                ptr_ += size_delta;
                // DCHECK_ALIGNED(ptr_, kAlignment);
                return ptr;
            }
        }
        auto *new_ptr = Alloc(new_size);  // Note: Alloc will take care of aligning new_size.
        std::memcpy(new_ptr, ptr, ptr_size);
        // TODO: Call free on ptr if linear alloc supports free.
        return new_ptr;
    }

    template <typename T>
    T *Alloc() {
        return AllocArray<T>(1);
    }

    template <typename T>
    T *AllocArray(size_t length) {
        return static_cast<T *>(Alloc(length * sizeof(T)));
    }

    // The BytesUsed method sums up bytes allocated from arenas in arena_head_ and nodes.
    // TODO: Change BytesAllocated to this behavior?
    [[nodiscard]] size_t BytesUsed() const {
        size_t total = ptr_ - begin_;
        if (arena_head_ != nullptr) {
            for (Arena *cur_arena = arena_head_->next_; cur_arena != nullptr;
                 cur_arena = cur_arena->next_) {
                total += cur_arena->GetBytesAllocated();
            }
        }
        return total;
    }

    [[nodiscard]] ArenaPool &GetArenaPool() const { return pool_; }

    [[nodiscard]] Arena *GetHeadArena() const { return arena_head_; }

    [[nodiscard]] uint8_t *CurrentPtr() const { return ptr_; }

    [[nodiscard]] size_t CurrentArenaUnusedBytes() const {
        // DCHECK_LE(ptr_, end_);
        return end_ - ptr_;
    }

    // Resets the current arena in use, which will force us to get a new arena
    // on next allocation.
    void ResetCurrentArena() {
        UpdateBytesAllocated();
        begin_ = nullptr;
        ptr_ = nullptr;
        end_ = nullptr;
    }

    bool Contains(const void *ptr) const {
        if (ptr >= begin_ && ptr < end_) {
            return true;
        }
        for (const Arena *cur_arena = arena_head_; cur_arena != nullptr;
             cur_arena = cur_arena->next_) {
            if (cur_arena->Contains(ptr)) {
                return true;
            }
        }
        return false;
    }

    // The alignment guaranteed for individual allocations.
    static constexpr size_t kAlignment = 8U;

    // The alignment required for the whole Arena rather than individual allocations.
    static constexpr size_t kArenaAlignment = 16U;

    // Extra bytes required by the memory tool.
    static constexpr size_t kMemoryToolRedZoneBytes = 8U;

private:
    uint8_t *AllocFromNewArena(size_t bytes) {
        Arena *new_arena = pool_.AllocArena(std::max(kArenaDefaultSize, bytes));
        if (new_arena == nullptr) [[unlikely]] {
            return nullptr;
        }

        // DCHECK(new_arena != nullptr);
        // DCHECK_LE(bytes, new_arena->Size());
        if (static_cast<size_t>(end_ - ptr_) > new_arena->Size() - bytes) {
            // The old arena has more space remaining than the new one, so keep using it.
            // This can happen when the requested size is over half of the default size.
            // DCHECK(arena_head_ != nullptr);
            new_arena->bytes_allocated_ = bytes;  // UpdateBytesAllocated() on the new_arena.
            new_arena->next_ = arena_head_->next_;
            arena_head_->next_ = new_arena;
        } else {
            UpdateBytesAllocated();
            new_arena->next_ = arena_head_;
            arena_head_ = new_arena;
            // Update our internal data structures.
            begin_ = new_arena->Begin();
            // DCHECK_ALIGNED(begin_, kAlignment);
            ptr_ = begin_ + bytes;
            end_ = new_arena->End();
        }
        return new_arena->Begin();
    }

    void UpdateBytesAllocated() {
        if (arena_head_ != nullptr) {
            // Update how many bytes we have allocated into the arena so that the arena pool knows
            // how much memory to zero out.
            arena_head_->bytes_allocated_ = ptr_ - begin_;
        }
    }

    ArenaPool pool_;
    uint8_t *begin_{nullptr};
    uint8_t *end_{nullptr};
    uint8_t *ptr_{nullptr};
    Arena *arena_head_{nullptr};

    ArenaAllocator(const ArenaAllocator &) = delete;
    void operator=(const ArenaAllocator &) = delete;
};

class MemMapArena final : public Arena {
public:
    MemMapArena(void *map_addr, size_t map_size)
        : Arena(static_cast<uint8_t *>(map_addr), map_size) {}

    ~MemMapArena() override { munmap(memory_, size_); }

    void Release() override {
        if (bytes_allocated_ > 0) {
            madvise(memory_, size_, MADV_DONTNEED);
            bytes_allocated_ = 0;
        }
    }

    static void *Allocate(size_t size, const char *name) {
        // Round up to a full page as that's the smallest unit of allocation for mmap()
        // and we want to be able to use all memory that we actually allocate.
        size = __builtin_align_up(size, getpagesize());
        auto *map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (map == MAP_FAILED) {
            return nullptr;
        }
        if constexpr (kDebugBuild) {
            prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, map, size, name);
        }
        return map;
    }
};

class MemMapArenaPool {
public:
    explicit MemMapArenaPool(const char *name = "lsplant-LinearAlloc")
        : name_(kDebugBuild ? name : nullptr) {}

    ~MemMapArenaPool() { ReclaimMemory(); }

    Arena *AllocArena(size_t size) {
        Arena *ret = nullptr;
        {
            std::lock_guard<std::mutex> const lock(lock_);
            if (free_arenas_ != nullptr && __builtin_expect(free_arenas_->Size() >= size, 1)) {
                ret = free_arenas_;
                free_arenas_ = free_arenas_->next_;
            }
        }
        if (ret == nullptr) {
            auto *memory = MemMapArena::Allocate(size, name_);
            if (memory == nullptr) [[unlikely]] {
                return nullptr;
            }
            return new MemMapArena(memory, size);
        }
        ret->Reset();
        return ret;
    }

    void FreeArenaChain(Arena *first) {
        static constexpr bool kArenaAllocatorPreciseTracking = false;
        if (kArenaAllocatorPreciseTracking) {
            // Do not reuse arenas when tracking.
            while (first != nullptr) {
                Arena *next = first->next_;
                delete first;
                first = next;
            }
            return;
        }

        if (first != nullptr) {
            Arena *last = first;
            while (last->next_ != nullptr) {
                last = last->next_;
            }
            std::lock_guard<std::mutex> const lock(lock_);
            last->next_ = free_arenas_;
            free_arenas_ = first;
        }
    }

    size_t GetBytesAllocated() const {
        size_t total = 0;
        std::lock_guard<std::mutex> const lock(lock_);
        for (Arena *arena = free_arenas_; arena != nullptr; arena = arena->next_) {
            total += arena->GetBytesAllocated();
        }
        return total;
    }

    void ReclaimMemory() {
        while (free_arenas_ != nullptr) {
            Arena *arena = free_arenas_;
            free_arenas_ = free_arenas_->next_;
            delete arena;
        }
    }

    void LockReclaimMemory() {
        std::lock_guard<std::mutex> const lock(lock_);
        ReclaimMemory();
    }

    // Trim the maps in arenas by madvising, used by JIT to reduce memory usage.
    void TrimMaps() {
        std::lock_guard<std::mutex> const lock(lock_);
        for (Arena *arena = free_arenas_; arena != nullptr; arena = arena->next_) {
            arena->Release();
        }
    }

private:
    const char *name_;
    Arena *free_arenas_{nullptr};
    // Use a std::mutex here as Arenas are second-from-the-bottom when using MemMaps, and MemMap
    // itself uses std::mutex scoped to within an allocate/free only.
    mutable std::mutex lock_;

    MemMapArenaPool(const MemMapArenaPool &) = delete;
    void operator=(const MemMapArenaPool &) = delete;
};

}  // namespace lsplant::art::base

#pragma clang diagnostic pop
