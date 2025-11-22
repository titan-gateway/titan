// Titan Memory Management - Header
// Monotonic arena allocator for allocation-free hot path

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace titan::core {

/// Monotonic arena allocator
/// Allocates memory in a single contiguous buffer, reset per request
/// No individual deallocation - entire arena is reset at once
class Arena {
public:
    explicit Arena(size_t initial_size = 64 * 1024); // 64KB default
    ~Arena();

    // Non-copyable, movable
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&&) noexcept;
    Arena& operator=(Arena&&) noexcept;

    /// Allocate memory with specified alignment
    [[nodiscard]] void* allocate(size_t size, size_t alignment = alignof(std::max_align_t));

    /// Typed allocation
    template<typename T>
    [[nodiscard]] T* allocate(size_t count = 1) {
        return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
    }

    /// Allocate array and return as span (safer than raw pointer)
    template<typename T>
    [[nodiscard]] std::span<T> allocate_array(size_t count) {
        T* ptr = static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
        return {ptr, count};
    }

    /// Allocate and construct object
    template<typename T, typename... Args>
    [[nodiscard]] T* construct(Args&&... args) {
        void* ptr = allocate(sizeof(T), alignof(T));
        return new (ptr) T(std::forward<Args>(args)...);
    }

    /// Allocate string storage and copy
    [[nodiscard]] std::string_view copy_string(std::string_view str);

    /// Reset the arena (invalidates all previous allocations)
    void reset() noexcept;

    /// Get current usage statistics
    [[nodiscard]] size_t bytes_allocated() const noexcept { return offset_; }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

private:
    void grow(size_t min_size);

    std::byte* buffer_ = nullptr;
    size_t capacity_ = 0;
    size_t offset_ = 0;
    size_t initial_size_;
};

/// RAII scope guard for arena reset
class ArenaScope {
public:
    explicit ArenaScope(Arena& arena) : arena_(arena) {}
    ~ArenaScope() { arena_.reset(); }

    ArenaScope(const ArenaScope&) = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;

private:
    Arena& arena_;
};

/// Pool allocator for fixed-size objects
/// Thread-local pool with free-list for object reuse
template<typename T, size_t PoolSize = 128>
class ObjectPool {
public:
    ObjectPool() = default;
    ~ObjectPool() { clear(); }

    // Non-copyable, non-movable
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    /// Acquire object from pool (constructs if needed)
    template<typename... Args>
    [[nodiscard]] T* acquire(Args&&... args) {
        if (free_list_) {
            Slot* slot = free_list_;
            free_list_ = slot->next;
            T* obj = reinterpret_cast<T*>(slot);
            new (obj) T(std::forward<Args>(args)...);
            return obj;
        }

        if (allocated_ < PoolSize) {
            Slot* slot = &storage_[allocated_++];
            T* obj = reinterpret_cast<T*>(slot);
            new (obj) T(std::forward<Args>(args)...);
            return obj;
        }

        // Pool exhausted, allocate on heap
        return new T(std::forward<Args>(args)...);
    }

    /// Release object back to pool
    void release(T* obj) noexcept {
        if (!obj) return;

        obj->~T();

        // Check if object is from pool
        Slot* slot = reinterpret_cast<Slot*>(obj);
        if (slot >= storage_ && slot < storage_ + PoolSize) {
            slot->next = free_list_;
            free_list_ = slot;
        } else {
            delete obj;
        }
    }

    /// Clear all objects
    void clear() noexcept {
        allocated_ = 0;
        free_list_ = nullptr;
    }

private:
    // Union to ensure proper alignment for both T and free-list pointers
    union Slot {
        T value;
        Slot* next;
        Slot() {} // Trivial constructor
        ~Slot() {} // Trivial destructor
    };

    Slot storage_[PoolSize];
    Slot* free_list_ = nullptr;
    size_t allocated_ = 0;
};

} // namespace titan::core
