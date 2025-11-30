cmake_minimum_required(VERSION 3.28)

project(
  Titan
  VERSION 0.1.0
  DESCRIPTION "High-performance C++23 API Gateway"
  LANGUAGES CXX
)

# C++23 requirement
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# vcpkg integration
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "vcpkg toolchain file")
endif()

# Force pthread support for arm64-linux
set(THREADS_PREFER_PTHREAD_FLAG ON)
set(CMAKE_THREAD_LIBS_INIT "-pthread")
set(CMAKE_HAVE_THREADS_LIBRARY 1)
set(CMAKE_USE_WIN32_THREADS_INIT 0)
set(CMAKE_USE_PTHREADS_INIT 1)
set(Threads_FOUND TRUE)

# Find dependencies
find_package(mimalloc CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(llhttp CONFIG REQUIRED)

# nghttp2 (HTTP/2 library) - use PkgConfig
find_package(PkgConfig REQUIRED)
pkg_check_modules(NGHTTP2 REQUIRED IMPORTED_TARGET libnghttp2)

# Compiler-specific flags
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
  )

  # Use libstdc++ (GCC's standard library) to match vcpkg dependencies
  # Note: libstdc++ from GCC 13+ has std::expected support
  message(STATUS "Using libstdc++ (GCC C++ standard library)")

  # Release optimizations
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(
      -O3
      -march=native
      -flto=thin
    )
    add_link_options(-fuse-ld=lld -flto=thin)
  endif()

  # Debug flags
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0)
  endif()
endif()

# Build options
option(BUILD_STATIC "Build fully static binary" OFF)
option(BUILD_BENCHMARKS "Build benchmark executables" OFF)

if(BUILD_STATIC)
  message(STATUS "Building static binary - linking libstdc++ and OpenSSL statically")

  # Static C++ standard library
  add_link_options(-static-libstdc++ -static-libgcc)

  # Try to link OpenSSL statically (prefer static libs)
  set(OPENSSL_USE_STATIC_LIBS TRUE)

  # Static linking for pthread
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread -static-libstdc++")

  message(STATUS "Static linking enabled: libstdc++, libgcc, pthread")
endif()

# Main library
add_subdirectory(src)

# Tests
enable_testing()
add_subdirectory(tests)

# Benchmarks (optional)
if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
