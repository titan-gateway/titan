// Platform Compatibility Layer
// Handles platform-specific type definitions and workarounds
//
// IMPORTANT: Include this BEFORE any system headers that might need these types

#pragma once

// x86-64 + libc++ compatibility: Define glibc extension types
// These are needed because SSE headers and other glibc headers (wchar.h, stdlib.h)
// expect _Float32/_Float64 types, but libc++ doesn't define them.
#if defined(__x86_64__) && defined(_LIBCPP_VERSION)
// Only define if not already defined by system headers
#ifndef _Float32
// _Float32 and _Float64 are glibc extension types for IEEE 754 binary32/binary64
// We define them as typedefs in the global namespace for compatibility
typedef float _Float32;
typedef double _Float64;
#endif
#endif
