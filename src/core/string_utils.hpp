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

// String Utilities - Fuzzy Matching and Similarity

#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace titan::core {

/// Calculate Levenshtein distance between two strings
/// Returns the minimum number of single-character edits (insertions, deletions, substitutions)
/// required to change one string into the other
[[nodiscard]] inline size_t levenshtein_distance(std::string_view s1, std::string_view s2) {
    const size_t len1 = s1.length();
    const size_t len2 = s2.length();

    // Optimization: if one string is empty, distance is length of the other
    if (len1 == 0)
        return len2;
    if (len2 == 0)
        return len1;

    // Space optimization: use only two rows instead of full matrix
    // This reduces memory from O(m*n) to O(n) and improves cache locality
    std::vector<size_t> prev_row(len2 + 1);
    std::vector<size_t> curr_row(len2 + 1);

    // Initialize first row
    for (size_t j = 0; j <= len2; ++j) {
        prev_row[j] = j;
    }

    // Fill the matrix row by row
    for (size_t i = 1; i <= len1; ++i) {
        curr_row[0] = i;  // First column value

        for (size_t j = 1; j <= len2; ++j) {
            if (s1[i - 1] == s2[j - 1]) {
                // Characters match, no operation needed
                curr_row[j] = prev_row[j - 1];
            } else {
                // Take minimum of: insert, delete, substitute
                curr_row[j] = 1 + std::min({
                                      prev_row[j],      // delete
                                      curr_row[j - 1],  // insert
                                      prev_row[j - 1]   // substitute
                                  });
            }
        }

        // Swap rows for next iteration
        std::swap(prev_row, curr_row);
    }

    return prev_row[len2];
}

/// Find similar strings from a list based on Levenshtein distance
/// Returns strings with edit distance <= max_distance, sorted by distance
[[nodiscard]] inline std::vector<std::string> find_similar_strings(
    std::string_view target, const std::vector<std::string>& candidates, size_t max_distance = 3) {
    std::vector<std::pair<std::string, size_t>> matches;

    for (const auto& candidate : candidates) {
        size_t distance = levenshtein_distance(target, candidate);
        if (distance <= max_distance && distance > 0) {  // distance > 0 excludes exact matches
            matches.emplace_back(candidate, distance);
        }
    }

    // Sort by distance (closest first)
    std::sort(matches.begin(), matches.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    // Extract strings only
    std::vector<std::string> result;
    result.reserve(matches.size());
    for (const auto& [str, _] : matches) {
        result.push_back(str);
    }

    return result;
}

/// Join strings with a delimiter
[[nodiscard]] inline std::string join(const std::vector<std::string>& strings,
                                      std::string_view delimiter) {
    if (strings.empty()) {
        return "";
    }

    std::string result = strings[0];
    for (size_t i = 1; i < strings.size(); ++i) {
        result += delimiter;
        result += strings[i];
    }
    return result;
}

}  // namespace titan::core
