/*
 * Copyright 2025 Titan Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


// Titan Memory Management - Implementation
// Monotonic arena allocator implementation
// Test: Verify CI workflow triggers on pull requests

#include "memory.hpp"

#include <cstdlib>
#include <cstring>
#include <new>

namespace titan::core {

Arena::Arena(size_t initial_size) : initial_size_(initial_size) {
    // Use aligned_alloc to ensure buffer can satisfy high alignment requirements
    // Align size up to cache line boundary (64 bytes) for better performance
    size_t aligned_size = (initial_size + 63) & ~63;
    buffer_ = static_cast<std::byte*>(std::aligned_alloc(64, aligned_size));
    if (!buffer_) {
        throw std::bad_alloc();
    }
    capacity_ = aligned_size;
    offset_ = 0;
}

Arena::~Arena() {
    if (buffer_) {
        std::free(buffer_);
    }
}

Arena::Arena(Arena&& other) noexcept
    : buffer_(other.buffer_),
      capacity_(other.capacity_),
      offset_(other.offset_),
      initial_size_(other.initial_size_) {
    other.buffer_ = nullptr;
    other.capacity_ = 0;
    other.offset_ = 0;
}

Arena& Arena::operator=(Arena&& other) noexcept {
    if (this != &other) {
        if (buffer_) {
            std::free(buffer_);
        }

        buffer_ = other.buffer_;
        capacity_ = other.capacity_;
        offset_ = other.offset_;
        initial_size_ = other.initial_size_;

        other.buffer_ = nullptr;
        other.capacity_ = 0;
        other.offset_ = 0;
    }
    return *this;
}

void* Arena::allocate(size_t size, size_t alignment) {
    // Align the current offset
    size_t aligned_offset = (offset_ + alignment - 1) & ~(alignment - 1);

    // Check if we need to grow
    if (aligned_offset + size > capacity_) {
        grow(size);
        aligned_offset = (offset_ + alignment - 1) & ~(alignment - 1);
    }

    void* ptr = buffer_ + aligned_offset;
    offset_ = aligned_offset + size;
    return ptr;
}

std::string_view Arena::copy_string(std::string_view str) {
    if (str.empty()) {
        return {};
    }

    char* ptr = allocate<char>(str.size());
    std::memcpy(ptr, str.data(), str.size());
    return std::string_view(ptr, str.size());
}

void Arena::reset() noexcept {
    offset_ = 0;
}

void Arena::grow(size_t min_size) {
    // Double capacity or use min_size, whichever is larger
    size_t new_capacity = capacity_ * 2;
    if (new_capacity < capacity_ + min_size) {
        new_capacity = capacity_ + min_size;
    }

    // Align new capacity to 64-byte boundary
    new_capacity = (new_capacity + 63) & ~63;

    // Can't use realloc with aligned_alloc, must allocate + copy + free
    std::byte* new_buffer = static_cast<std::byte*>(std::aligned_alloc(64, new_capacity));
    if (!new_buffer) {
        throw std::bad_alloc();
    }

    // Copy existing data
    std::memcpy(new_buffer, buffer_, offset_);

    // Free old buffer
    std::free(buffer_);

    buffer_ = new_buffer;
    capacity_ = new_capacity;
}

} // namespace titan::core
