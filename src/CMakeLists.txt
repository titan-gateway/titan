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
  core/server.hpp
  core/server.cpp
  core/server_runner.hpp
  core/server_runner.cpp
  core/admin_server.hpp
  core/admin_server.cpp
  core/logging.hpp
  core/logging.cpp
  core/jwt.hpp
  core/jwt.cpp
  core/jwks_fetcher.hpp
  core/jwks_fetcher.cpp
  http/http.hpp
  http/http.cpp
  http/parser.hpp
  http/parser.cpp
  http/h2.hpp
  http/h2.cpp
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
  control/config.hpp
  control/config.cpp
  control/health.hpp
  control/health.cpp
  control/metrics.hpp
  control/prometheus.hpp
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
)

# Titan executable
add_executable(titan main.cpp)
target_link_libraries(titan PRIVATE titan_lib)
