# Unit test executable
add_executable(titan_tests
  test_main.cpp
  test_memory.cpp
  test_core.cpp
  test_http.cpp
  test_h2.cpp
  test_tls.cpp
  test_gateway.cpp
  test_keepalive.cpp
  test_middleware.cpp
  test_config.cpp
  test_config_validation_security.cpp
  test_health.cpp
  test_metrics.cpp
  test_prometheus.cpp
  test_rate_limit.cpp
  test_proxy.cpp
  test_simd.cpp
  test_circuit_breaker.cpp
  test_compression.cpp
  test_logging.cpp
  test_jwt.cpp
  test_jwks.cpp
  test_jwt_authz.cpp
  test_jwt_revocation.cpp
  test_transform.cpp
)

target_link_libraries(titan_tests
  PRIVATE
    titan_lib
    Catch2::Catch2WithMain
)

# Register with CTest
include(Catch)
catch_discover_tests(titan_tests)
