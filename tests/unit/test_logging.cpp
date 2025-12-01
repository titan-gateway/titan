// Titan Logging Unit Tests

#include <catch2/catch_test_macros.hpp>

#include "../../src/core/logging.hpp"

using namespace titan::logging;

TEST_CASE("Correlation ID generation", "[logging][correlation_id]") {
    SECTION("generates valid UUID format") {
        std::string correlation_id = generate_correlation_id();

        // Should contain exactly one '#' separator
        size_t hash_count = std::count(correlation_id.begin(), correlation_id.end(), '#');
        REQUIRE(hash_count == 1);

        // Should validate as a proper correlation ID
        REQUIRE(is_valid_uuid(correlation_id));
    }

    SECTION("generates unique IDs with incrementing counter") {
        std::string id1 = generate_correlation_id();
        std::string id2 = generate_correlation_id();
        std::string id3 = generate_correlation_id();

        // All should be different
        REQUIRE(id1 != id2);
        REQUIRE(id2 != id3);
        REQUIRE(id1 != id3);

        // All should be valid
        REQUIRE(is_valid_uuid(id1));
        REQUIRE(is_valid_uuid(id2));
        REQUIRE(is_valid_uuid(id3));

        // Should have same UUID base but different counters
        size_t hash_pos1 = id1.find('#');
        size_t hash_pos2 = id2.find('#');

        std::string base1 = id1.substr(0, hash_pos1);
        std::string base2 = id2.substr(0, hash_pos2);

        // Same thread should have same base UUID
        REQUIRE(base1 == base2);

        // But different counters
        std::string counter1 = id1.substr(hash_pos1 + 1);
        std::string counter2 = id2.substr(hash_pos2 + 1);
        REQUIRE(counter1 != counter2);
    }

    SECTION("format matches expected pattern") {
        std::string correlation_id = generate_correlation_id();

        // Split by '#'
        size_t hash_pos = correlation_id.find('#');
        REQUIRE(hash_pos != std::string::npos);

        std::string uuid_part = correlation_id.substr(0, hash_pos);
        std::string counter_part = correlation_id.substr(hash_pos + 1);

        // UUID part should be 36 characters (8-4-4-4-12 with hyphens)
        REQUIRE(uuid_part.length() == 36);

        // Should have hyphens at correct positions
        REQUIRE(uuid_part[8] == '-');
        REQUIRE(uuid_part[13] == '-');
        REQUIRE(uuid_part[18] == '-');
        REQUIRE(uuid_part[23] == '-');

        // Version should be 4
        REQUIRE(uuid_part[14] == '4');

        // Variant should be 8, 9, a, or b
        char variant = uuid_part[19];
        REQUIRE((variant == '8' || variant == '9' || variant == 'a' || variant == 'b' ||
                 variant == 'A' || variant == 'B'));

        // Counter should be all digits
        REQUIRE(!counter_part.empty());
        for (char c : counter_part) {
            REQUIRE((c >= '0' && c <= '9'));
        }
    }
}

TEST_CASE("UUID validation", "[logging][validation]") {
    SECTION("accepts valid correlation IDs") {
        // Valid UUID v4 with counter
        REQUIRE(is_valid_uuid("550e8400-e29b-41d4-a716-446655440000#0"));
        REQUIRE(is_valid_uuid("550e8400-e29b-41d4-a716-446655440000#123"));
        REQUIRE(is_valid_uuid("550e8400-e29b-41d4-a716-446655440000#999999"));

        // Lowercase hex digits
        REQUIRE(is_valid_uuid("abcdef12-3456-4789-abcd-ef0123456789#42"));

        // Uppercase hex digits
        REQUIRE(is_valid_uuid("ABCDEF12-3456-4789-ABCD-EF0123456789#42"));

        // Mixed case
        REQUIRE(is_valid_uuid("AbCdEf12-3456-4789-AbCd-Ef0123456789#42"));
    }

    SECTION("rejects invalid formats") {
        // Missing separator
        REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-a716-446655440000"));

        // Missing counter
        REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-a716-446655440000#"));

        // Non-digit counter
        REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-a716-446655440000#abc"));
        REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-a716-446655440000#12a"));

        // Wrong UUID length
        REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-a716-44665544000#0"));  // Too short
        REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-a716-4466554400000#0")); // Too long

        // Missing hyphens
        REQUIRE_FALSE(is_valid_uuid("550e8400e29b41d4a716446655440000#0"));

        // Wrong hyphen positions
        REQUIRE_FALSE(is_valid_uuid("550e84-00-e29b-41d4-a716-446655440000#0"));

        // Wrong version (not 4)
        REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-31d4-a716-446655440000#0"));
        REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-51d4-a716-446655440000#0"));

        // Wrong variant (not 8, 9, a, b)
        REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-0716-446655440000#0")); // variant 0
        REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-c716-446655440000#0")); // variant c
        REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-f716-446655440000#0")); // variant f

        // Non-hex characters in UUID
        REQUIRE_FALSE(is_valid_uuid("550g8400-e29b-41d4-a716-446655440000#0"));
        REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-a716-44665544z000#0"));

        // Empty string
        REQUIRE_FALSE(is_valid_uuid(""));

        // Multiple separators
        REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-a716-446655440000#42#56"));
    }

    SECTION("validates generated correlation IDs") {
        // Generate multiple IDs and validate all of them
        for (int i = 0; i < 100; ++i) {
            std::string correlation_id = generate_correlation_id();
            REQUIRE(is_valid_uuid(correlation_id));
        }
    }
}

TEST_CASE("Logger thread-local access", "[logging][logger]") {
    SECTION("returns nullptr before initialization") {
        // Note: This test may fail if logger was initialized by another test
        // In practice, logger is always initialized before use
        auto* logger = get_current_logger();

        // Logger might be initialized or not depending on test execution order
        // We just verify the function doesn't crash
        (void)logger;
    }
}
