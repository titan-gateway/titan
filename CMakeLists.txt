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
find_package(glaze CONFIG REQUIRED)
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
    -stdlib=libc++
  )

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

# Main library
add_subdirectory(src)

# Tests
enable_testing()
add_subdirectory(tests)
