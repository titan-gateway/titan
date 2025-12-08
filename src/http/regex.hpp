#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Forward declare PCRE2 types to avoid header pollution
struct pcre2_real_code_8;
struct pcre2_real_match_data_8;
struct pcre2_real_match_context_8;

namespace titan::http {

// PCRE2 wrapper for regex compilation and execution
// Thread-safe for read operations after compilation
class Regex {
public:
    // Compile a regex pattern
    // Returns nullopt if compilation fails
    [[nodiscard]] static std::optional<Regex> compile(std::string_view pattern);

    // Compile a regex pattern with error message
    [[nodiscard]] static std::optional<Regex> compile(std::string_view pattern,
                                                      std::string& error_message);

    // Move-only type (manages PCRE2 resources)
    Regex(Regex&& other) noexcept;
    Regex& operator=(Regex&& other) noexcept;
    ~Regex();

    // Delete copy operations
    Regex(const Regex&) = delete;
    Regex& operator=(const Regex&) = delete;

    // Check if pattern matches the subject string
    [[nodiscard]] bool matches(std::string_view subject) const;

    // Find first match and return matched substring
    [[nodiscard]] std::optional<std::string_view> find_first(std::string_view subject) const;

    // Extract capture groups from first match
    // Returns empty vector if no match
    // Index 0 is the full match, 1+ are capture groups
    [[nodiscard]] std::vector<std::string_view> extract_groups(std::string_view subject) const;

    // Perform substitution with capture group support
    // Supports $1, $2, ... for backreferences
    // Returns the replaced string, or original if no match
    [[nodiscard]] std::string substitute(std::string_view subject,
                                         std::string_view replacement) const;

    // Get the original pattern string
    [[nodiscard]] std::string_view pattern() const { return pattern_; }

private:
    explicit Regex(pcre2_real_code_8* code, std::string pattern);

    pcre2_real_code_8* code_;  // Compiled regex (owned)
    std::string pattern_;      // Original pattern (for debugging)
};

// Utility function for URL encoding/decoding
namespace url {

// URL encode a string (percent-encoding)
[[nodiscard]] std::string encode(std::string_view str);

// URL decode a string (percent-decoding)
// Returns nullopt if invalid encoding (e.g., incomplete % sequence)
[[nodiscard]] std::optional<std::string> decode(std::string_view str);

}  // namespace url

}  // namespace titan::http
