#pragma once

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/sinks/FileSink.h>
#include <quill/sinks/RotatingJsonFileSink.h>

#include <chrono>
#include <random>
#include <string>
#include <string_view>

namespace titan::logging {

// Initialize Quill logging backend (called once at startup)
void init_logging_system();

// Initialize per-worker logger with JSON file sink
// Returns logger for the given worker ID
quill::Logger* init_worker_logger(int worker_id);

// Shutdown logging system (called at exit)
void shutdown_logging();

// UUID v4 generation for correlation IDs
std::string generate_correlation_id();

// Validate UUID format (8-4-4-4-12)
bool is_valid_uuid(std::string_view uuid);

// Get current thread's logger (returns nullptr if not initialized)
quill::Logger* get_current_logger();

// Logging macros for structured logging

// Request completion logging
#define LOG_REQUEST(logger, method, path, status, duration_us, client_ip, \
                    correlation_id)                                       \
  LOG_INFO(logger,                                                        \
           "Request completed: method={}, path={}, status={}, "          \
           "duration_us={}, client_ip={}, correlation_id={}",            \
           method, path, status, duration_us, client_ip, correlation_id)

// Error logging with context
#define LOG_ERROR_CTX(logger, message, correlation_id, error_code, \
                      error_detail)                                \
  LOG_ERROR(logger,                                                \
            "{}: correlation_id={}, error_code={}, error_detail={}", \
            message, correlation_id, error_code, error_detail)

// Upstream connection event logging
#define LOG_UPSTREAM(logger, event, upstream, backend_host, backend_port, \
                     correlation_id)                                      \
  LOG_INFO(logger,                                                        \
           "Upstream {}: upstream={}, backend={}:{}, correlation_id={}",  \
           event, upstream, backend_host, backend_port, correlation_id)

// Debug logging (eliminated in release builds)
#if defined(NDEBUG)
#define LOG_DEBUG(logger, message, ...) ((void)0)
#else
#define LOG_DEBUG(logger, message, ...) \
  LOG_INFO(logger, "DEBUG: " message, ##__VA_ARGS__)
#endif

}  // namespace titan::logging
