#pragma once

#include <ankerl/unordered_dense.h>

namespace titan::core {

// High-performance container type aliases using ankerl::unordered_dense
// This library provides ~25% faster lookups and 300-400% faster iteration
// compared to std::unordered_map/set while maintaining 99% API compatibility.
//
// Key characteristics:
// - Dense storage: contiguous key-value pairs (better cache locality)
// - Fast iteration: 400% faster than std::unordered_map
// - Fast lookups: 25% faster than std::unordered_map
// - Low memory overhead: ~30% less memory than std::unordered_map
// - Iterator invalidation: like std::vector (invalidates on insertion)
//
// Usage:
//   titan::core::fast_map<int, Connection*> connections;
//   titan::core::fast_set<std::string_view> active_streams;

template <typename Key, typename Value>
using fast_map = ankerl::unordered_dense::map<Key, Value>;

template <typename Key>
using fast_set = ankerl::unordered_dense::set<Key>;

// Future optimization: For very large maps (>1M entries), consider segmented_map
// which provides better memory layout for huge datasets at the cost of slightly
// slower lookups. Uncomment if needed:
//
// template <typename Key, typename Value>
// using large_map = ankerl::unordered_dense::segmented_map<Key, Value>;
//
// template <typename Key>
// using large_set = ankerl::unordered_dense::segmented_set<Key>;

}  // namespace titan::core
