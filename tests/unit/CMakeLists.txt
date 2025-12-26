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
  test_fuzzy_matching_security.cpp
  test_middleware_chain_security.cpp
  test_replacement_model_security.cpp
  test_injection_attacks.cpp
  test_race_conditions.cpp
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
  test_jwt_websocket.cpp
  test_transform.cpp
  test_websocket.cpp
)

target_link_libraries(titan_tests
  PRIVATE
    titan_lib
    Catch2::Catch2WithMain
)

# Register with CTest
include(Catch)
catch_discover_tests(titan_tests)
