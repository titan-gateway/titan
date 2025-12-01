#include "logging.hpp"

#include <fmt/format.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>
#include <algorithm>

#include "../control/config.hpp"

namespace titan::logging {

static thread_local quill::Logger* g_current_logger = nullptr;

void init_logging_system() {
  quill::Backend::start();
}

quill::Logger* init_worker_logger(int worker_id, const control::LogConfig& log_config) {
  std::filesystem::create_directories(log_config.output);

  quill::RotatingFileSinkConfig config;
  config.set_rotation_max_file_size(log_config.rotation.max_size_mb * 1'000'000);
  config.set_max_backup_files(log_config.rotation.max_files);
  config.set_open_mode('a');

  std::string log_path = fmt::format("{}/worker_{}.log", log_config.output, worker_id);

  quill::Logger* logger = nullptr;

  if (log_config.format == "json") {
    auto json_sink = quill::Frontend::create_or_get_sink<quill::RotatingJsonFileSink>(
        log_path, config);
    logger = quill::Frontend::create_or_get_logger(
        fmt::format("worker_{}", worker_id), std::move(json_sink));
  } else {
    auto file_sink = quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
        log_path, config);
    logger = quill::Frontend::create_or_get_logger(
        fmt::format("worker_{}", worker_id), std::move(file_sink));
  }

  std::string level_lower = log_config.level;
  std::transform(level_lower.begin(), level_lower.end(), level_lower.begin(), ::tolower);

  if (level_lower == "debug") {
    logger->set_log_level(quill::LogLevel::Debug);
  } else if (level_lower == "info") {
    logger->set_log_level(quill::LogLevel::Info);
  } else if (level_lower == "warning" || level_lower == "warn") {
    logger->set_log_level(quill::LogLevel::Warning);
  } else if (level_lower == "error") {
    logger->set_log_level(quill::LogLevel::Error);
  } else {
    logger->set_log_level(quill::LogLevel::Info);
  }

  g_current_logger = logger;
  return logger;
}

void shutdown_logging() {
  quill::Frontend::get_all_loggers();
  quill::Backend::stop();
}

quill::Logger* get_current_logger() {
  return g_current_logger;
}

// Generate base UUID v4 (called once per worker thread)
static std::string generate_base_uuid() {
  // XOR combines hardware randomness with timestamp for thread-unique seed
  std::mt19937 rng(std::random_device{}() ^
                   std::chrono::steady_clock::now().time_since_epoch().count());
  std::uniform_int_distribution<uint32_t> dist;

  // Generate 128 bits (16 bytes) of random data
  std::array<uint8_t, 16> uuid_bytes{};
  for (size_t i = 0; i < 16; i += 4) {
    uint32_t random_val = dist(rng);
    uuid_bytes[i] = static_cast<uint8_t>(random_val & 0xFF);
    uuid_bytes[i + 1] = static_cast<uint8_t>((random_val >> 8) & 0xFF);
    uuid_bytes[i + 2] = static_cast<uint8_t>((random_val >> 16) & 0xFF);
    uuid_bytes[i + 3] = static_cast<uint8_t>((random_val >> 24) & 0xFF);
  }

  // Set version to 4 (random UUID)
  uuid_bytes[6] = (uuid_bytes[6] & 0x0F) | 0x40;
  // Set variant to RFC4122
  uuid_bytes[8] = (uuid_bytes[8] & 0x3F) | 0x80;

  // Format as 8-4-4-4-12 string
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');

  for (size_t i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      oss << '-';
    }
    oss << std::setw(2) << static_cast<int>(uuid_bytes[i]);
  }

  return oss.str();
}

std::string generate_correlation_id() {
  // Generate base UUID once per worker, append incrementing counter
  // Format: {base_uuid}#{counter}
  // Performance: ~5ns (just counter increment + formatting)
  static thread_local std::string base_uuid = generate_base_uuid();
  static thread_local std::atomic<uint64_t> counter{0};

  return fmt::format("{}#{}", base_uuid, counter.fetch_add(1));
}

bool is_valid_uuid(std::string_view uuid) {
  // Validate correlation ID format: {uuid}#{counter}
  // Example: 550e8400-e29b-41d4-a716-446655440000#42

  // Find the '#' separator (search from back for performance)
  size_t hash_pos = uuid.rfind('#');
  if (hash_pos == std::string_view::npos) {
    return false;  // No separator found
  }

  // Split into UUID part and counter part
  std::string_view uuid_part = uuid.substr(0, hash_pos);
  std::string_view counter_part = uuid.substr(hash_pos + 1);

  // Validate UUID part (36 characters: 8-4-4-4-12 with hyphens)
  if (uuid_part.length() != 36) {
    return false;
  }

  // Check hyphens at correct positions
  if (uuid_part[8] != '-' || uuid_part[13] != '-' || uuid_part[18] != '-' ||
      uuid_part[23] != '-') {
    return false;
  }

  // Check version (character 14 must be '4')
  if (uuid_part[14] != '4') {
    return false;
  }

  // Check variant (character 19 must be '8', '9', 'a', or 'b')
  char variant = uuid_part[19];
  if (variant != '8' && variant != '9' && variant != 'a' && variant != 'b' &&
      variant != 'A' && variant != 'B') {
    return false;
  }

  // Check all other positions in UUID part are hex digits
  auto is_hex = [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
  };

  for (size_t i = 0; i < 36; ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23)
      continue;  // Skip hyphens
    if (!is_hex(uuid_part[i])) return false;
  }

  // Validate counter part (must be all digits)
  if (counter_part.empty()) {
    return false;
  }

  for (char c : counter_part) {
    if (c < '0' || c > '9') {
      return false;
    }
  }

  return true;
}

}  // namespace titan::logging
