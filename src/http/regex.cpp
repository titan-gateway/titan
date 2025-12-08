#include "regex.hpp"

#include <cstdint>
#include <cstring>
#include <sstream>

// PCRE2 API - use 8-bit code units
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

namespace titan::http {

// Helper to convert error code to string
static std::string get_pcre2_error(int error_code) {
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message(error_code, buffer, sizeof(buffer));
    return std::string(reinterpret_cast<const char*>(buffer));
}

// Regex implementation

Regex::Regex(pcre2_real_code_8* code, std::string pattern)
    : code_(code), pattern_(std::move(pattern)) {}

Regex::Regex(Regex&& other) noexcept : code_(other.code_), pattern_(std::move(other.pattern_)) {
    other.code_ = nullptr;
}

Regex& Regex::operator=(Regex&& other) noexcept {
    if (this != &other) {
        if (code_) {
            pcre2_code_free(code_);
        }
        code_ = other.code_;
        pattern_ = std::move(other.pattern_);
        other.code_ = nullptr;
    }
    return *this;
}

Regex::~Regex() {
    if (code_) {
        pcre2_code_free(code_);
    }
}

std::optional<Regex> Regex::compile(std::string_view pattern) {
    std::string error_message;
    return compile(pattern, error_message);
}

std::optional<Regex> Regex::compile(std::string_view pattern, std::string& error_message) {
    int error_code;
    PCRE2_SIZE error_offset;

    auto* code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.data()), pattern.size(),
                               0,  // options (default)
                               &error_code, &error_offset, nullptr);

    if (!code) {
        error_message = get_pcre2_error(error_code) + " at offset " + std::to_string(error_offset) +
                        " in pattern: " + std::string(pattern);
        return std::nullopt;
    }

    return Regex(code, std::string(pattern));
}

bool Regex::matches(std::string_view subject) const {
    auto* match_data = pcre2_match_data_create_from_pattern(code_, nullptr);
    if (!match_data) {
        return false;
    }

    int rc = pcre2_match(code_, reinterpret_cast<PCRE2_SPTR>(subject.data()), subject.size(),
                         0,  // start offset
                         0,  // options
                         match_data, nullptr);

    pcre2_match_data_free(match_data);

    return rc >= 0;  // >= 0 means match found
}

std::optional<std::string_view> Regex::find_first(std::string_view subject) const {
    auto* match_data = pcre2_match_data_create_from_pattern(code_, nullptr);
    if (!match_data) {
        return std::nullopt;
    }

    int rc = pcre2_match(code_, reinterpret_cast<PCRE2_SPTR>(subject.data()), subject.size(),
                         0,  // start offset
                         0,  // options
                         match_data, nullptr);

    if (rc < 0) {
        pcre2_match_data_free(match_data);
        return std::nullopt;
    }

    PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
    PCRE2_SIZE start = ovector[0];
    PCRE2_SIZE end = ovector[1];

    pcre2_match_data_free(match_data);

    return subject.substr(start, end - start);
}

std::vector<std::string_view> Regex::extract_groups(std::string_view subject) const {
    std::vector<std::string_view> groups;

    auto* match_data = pcre2_match_data_create_from_pattern(code_, nullptr);
    if (!match_data) {
        return groups;
    }

    int rc = pcre2_match(code_, reinterpret_cast<PCRE2_SPTR>(subject.data()), subject.size(),
                         0,  // start offset
                         0,  // options
                         match_data, nullptr);

    if (rc < 0) {
        pcre2_match_data_free(match_data);
        return groups;
    }

    PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);

    // rc is the number of captured groups + 1 (full match)
    for (int i = 0; i < rc; ++i) {
        PCRE2_SIZE start = ovector[2 * i];
        PCRE2_SIZE end = ovector[2 * i + 1];

        if (start == PCRE2_UNSET) {
            groups.emplace_back();  // Unmatched group
        } else {
            groups.push_back(subject.substr(start, end - start));
        }
    }

    pcre2_match_data_free(match_data);

    return groups;
}

std::string Regex::substitute(std::string_view subject, std::string_view replacement) const {
    // PCRE2 substitute API performs regex substitution with backreferences
    PCRE2_SIZE out_len = subject.size() * 2 + replacement.size() * 2;  // Initial estimate
    std::string output(out_len, '\0');

    int rc = pcre2_substitute(code_, reinterpret_cast<PCRE2_SPTR>(subject.data()), subject.size(),
                              0,                        // start offset
                              PCRE2_SUBSTITUTE_GLOBAL,  // replace all occurrences
                              nullptr,                  // match data
                              nullptr,                  // match context
                              reinterpret_cast<PCRE2_SPTR>(replacement.data()), replacement.size(),
                              reinterpret_cast<PCRE2_UCHAR*>(output.data()), &out_len);

    if (rc < 0) {
        if (rc == PCRE2_ERROR_NOMEMORY) {
            // Output buffer too small, retry with larger buffer
            output.resize(out_len);
            rc = pcre2_substitute(code_, reinterpret_cast<PCRE2_SPTR>(subject.data()),
                                  subject.size(), 0, PCRE2_SUBSTITUTE_GLOBAL, nullptr, nullptr,
                                  reinterpret_cast<PCRE2_SPTR>(replacement.data()),
                                  replacement.size(), reinterpret_cast<PCRE2_UCHAR*>(output.data()),
                                  &out_len);
        }

        if (rc < 0) {
            // No match or error - return original string
            return std::string(subject);
        }
    }

    output.resize(out_len);
    return output;
}

// URL encoding/decoding utilities

namespace url {

std::string encode(std::string_view str) {
    std::ostringstream encoded;

    for (unsigned char c : str) {
        // Unreserved characters: A-Z a-z 0-9 - _ . ~
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            // Percent-encode
            encoded << '%';
            encoded << "0123456789ABCDEF"[c >> 4];
            encoded << "0123456789ABCDEF"[c & 0x0F];
        }
    }

    return encoded.str();
}

static int hex_digit_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;  // Invalid hex digit
}

std::optional<std::string> decode(std::string_view str) {
    std::string decoded;
    decoded.reserve(str.size());

    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%') {
            // Expect 2 hex digits
            if (i + 2 >= str.size()) {
                return std::nullopt;  // Incomplete percent sequence
            }

            int high = hex_digit_value(str[i + 1]);
            int low = hex_digit_value(str[i + 2]);

            if (high < 0 || low < 0) {
                return std::nullopt;  // Invalid hex digits
            }

            decoded += static_cast<char>((high << 4) | low);
            i += 2;  // Skip the two hex digits
        } else if (str[i] == '+') {
            // '+' is space in query strings (application/x-www-form-urlencoded)
            decoded += ' ';
        } else {
            decoded += str[i];
        }
    }

    return decoded;
}

}  // namespace url

}  // namespace titan::http
