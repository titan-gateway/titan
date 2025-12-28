# Titan Library
add_library(titan_lib
  core/core.hpp
  core/core.cpp
  core/memory.hpp
  core/memory.cpp
  core/socket.hpp
  core/socket.cpp
  core/tls.hpp
  core/tls.cpp
  core/connection_manager.hpp
  core/connection_manager.cpp
  core/backend_proxy_service.hpp
  core/backend_proxy_service.cpp
  core/server.hpp
  core/server.cpp
  core/admin_server.hpp
  core/admin_server.cpp
  core/logging.hpp
  core/logging.cpp
  core/compression.hpp
  core/compression.cpp
  core/jwt.hpp
  core/jwt.cpp
  core/jwks_fetcher.hpp
  core/jwks_fetcher.cpp
  core/jwt_revocation.hpp
  core/jwt_revocation.cpp
  http/http.hpp
  http/http.cpp
  http/parser.hpp
  http/parser.cpp
  http/h2.hpp
  http/h2.cpp
  http/grpc.hpp
  http/grpc.cpp
  http/regex.hpp
  http/regex.cpp
  http/websocket.hpp
  http/websocket.cpp
  gateway/router.hpp
  gateway/router.cpp
  gateway/upstream.hpp
  gateway/upstream.cpp
  gateway/connection_pool.hpp
  gateway/connection_pool.cpp
  gateway/circuit_breaker.hpp
  gateway/circuit_breaker.cpp
  gateway/rate_limit.hpp
  gateway/rate_limit.cpp
  gateway/pipeline.hpp
  gateway/pipeline.cpp
  gateway/jwt_middleware.hpp
  gateway/jwt_middleware.cpp
  gateway/jwt_authz_middleware.hpp
  gateway/jwt_authz_middleware.cpp
  gateway/transform_middleware.hpp
  gateway/transform_middleware.cpp
  gateway/compression_middleware.hpp
  gateway/compression_middleware.cpp
  gateway/factory.hpp
  gateway/factory.cpp
  control/config.hpp
  control/config.cpp
  control/config_validator.hpp
  control/config_validator.cpp
  control/health.hpp
  control/health.cpp
  control/metrics.hpp
  control/prometheus.hpp
  runtime/orchestrator.hpp
  runtime/orchestrator.cpp
)

# Public include directories
target_include_directories(titan_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(titan_lib
  PUBLIC
    mimalloc-static
    OpenSSL::SSL
    OpenSSL::Crypto
    fmt::fmt
    nlohmann_json::nlohmann_json
    llhttp::llhttp_static
    PkgConfig::NGHTTP2
    quill::quill
    httplib::httplib
    PCRE2::8BIT
    ZLIB::ZLIB
    $<IF:$<TARGET_EXISTS:zstd::libzstd_static>,zstd::libzstd_static,zstd::libzstd_shared>
    unofficial::brotli::brotlienc
    unordered_dense::unordered_dense
)

# Titan executable
add_executable(titan main.cpp)
target_link_libraries(titan PRIVATE titan_lib)
