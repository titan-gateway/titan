---
sidebar_position: 3
title: Building from Source
---

# Titan Build Guide

Comprehensive guide for building Titan with different optimization levels.

## Quick Start

```bash
# Development build (fast compilation, debug symbols)
cmake --preset=dev
cmake --build --preset=dev

# Run
./build/dev/src/titan --config config/test.json
```

## Build Presets

### Development Builds

#### Dev (Default Development)
```bash
cmake --preset=dev
cmake --build --preset=dev
```
- **Optimization**: `-O0` (none)
- **Debug symbols**: Yes
- **Sanitizers**: ASan + UBSan
- **Build time**: Fast (2-3 min)
- **Binary size**: 31MB
- **Use for**: Local development, debugging

#### TSAN (Thread Sanitizer)
```bash
cmake --preset=tsan
cmake --build --preset=tsan
```
- **Optimization**: `-O1`
- **Debug symbols**: Yes
- **Sanitizers**: ThreadSanitizer
- **Build time**: Fast (2-3 min)
- **Use for**: Finding race conditions

### Production Builds

#### Release (Performance with Native Optimizations)
```bash
cmake --preset=release
cmake --build --preset=release
```
- **Optimization**: `-O3 -march=native -flto=thin`
- **Target**: Your specific CPU
- **Build time**: Medium (5-7 min)
- **Binary size**: 7.3MB (after strip)
- **Compatibility**: Your CPU only
- **Use for**: Maximum local performance

#### Static (Portable Native Build)
```bash
cmake --preset=static
cmake --build --preset=static
```
- **Optimization**: `-O3 -march=native -flto=thin`
- **Static linking**: libstdc++, libgcc, pthread
- **Target**: Your specific CPU
- **Build time**: Medium (5-7 min)
- **Binary size**: 7.3MB (after strip)
- **Dependencies**: Minimal (libc++, libc, libm)
- **Use for**: Bare metal deployment (systemd)

#### Release-Generic (x86-64 Distribution) **RECOMMENDED FOR DOCKER/K8S**
```bash
cmake --preset=release-generic
cmake --build --preset=release-generic
```
- **Optimization**: `-O3 -march=x86-64-v2 -flto=full`
- **Target**: x86-64-v2 (Intel 2009+, AMD 2011+)
- **Static linking**: Yes
- **Build time**: Slow (10-15 min with full LTO)
- **Binary size**: ~7.3MB (after strip)
- **Compatibility**: 99% of x86-64 servers
- **Performance**: 85% of max
- **Use for**: Docker images, Kubernetes, package distribution

#### Release-Generic-ARM (ARM64 Distribution) **RECOMMENDED FOR ARM**
```bash
cmake --preset=release-generic-arm
cmake --build --preset=release-generic-arm
```
- **Optimization**: `-O3 -march=armv8-a -flto=full`
- **Target**: ARMv8-A (AWS Graviton, Apple Silicon, all ARM64)
- **Static linking**: Yes
- **Build time**: Slow (10-15 min with full LTO)
- **Binary size**: ~7MB (after strip)
- **Compatibility**: All ARM64 servers
- **Performance**: 85% of max
- **Use for**: ARM Docker images, AWS Graviton, Apple Silicon

## Build Comparison

| Preset | March | LTO | Build Time | Binary Size | Compatibility | Performance | Use Case |
|--------|-------|-----|------------|-------------|---------------|-------------|----------|
| `dev` | native | none | 2-3 min | 31MB | Your CPU | 30% | Development |
| `tsan` | native | none | 2-3 min | 31MB | Your CPU | 40% | Race detection |
| `release` | native | thin | 5-7 min | 7.3MB | Your CPU | 100% | Local perf |
| `static` | native | thin | 5-7 min | 7.3MB | Your CPU | 100% | Systemd |
| **`release-generic`** | **x86-64-v2** | **full** | **10-15 min** | **7.3MB** | **99% x86** | **85%** | **Docker/K8s** |
| **`release-generic-arm`** | **armv8-a** | **full** | **10-15 min** | **7MB** | **All ARM64** | **85%** | **ARM servers** |

## Optimization Flags Explained

### Optimization Levels

| Flag | Description | Performance | Compile Time |
|------|-------------|-------------|--------------|
| `-O0` | No optimization | Baseline | 1x |
| `-O1` | Basic optimization | +20% | 1.1x |
| `-O2` | Moderate optimization | +50% | 1.3x |
| `-O3` | Maximum optimization | +70% | 1.5x |

### CPU Architecture Flags

| Flag | Target | Compatibility | Performance |
|------|--------|---------------|-------------|
| `-march=native` | Your exact CPU | Your CPU only | 100% |
| `-march=x86-64-v2` | Intel 2009+, AMD 2011+ | 99% of x86-64 | 85% |
| `-march=haswell` | Intel 2013+, AMD 2017+ | 95% of servers | 90% |
| `-march=skylake` | Intel 2015+, AMD 2019+ | 80% of servers | 95% |
| `-march=armv8-a` | All ARM64 | 100% of ARM64 | 85% |

### Link-Time Optimization (LTO)

| Flag | Description | Performance | Build Time |
|------|-------------|-------------|------------|
| `-flto=thin` | Fast LTO (parallel) | +5-8% | 2x |
| `-flto=full` | Full LTO (whole program) | +10-15% | 3x |

**Why Full LTO for Generic Builds:**
- Generic builds target broad compatibility (x86-64-v2, armv8-a)
- They lose CPU-specific optimizations from `-march=native`
- Full LTO compensates by doing aggressive cross-file optimization
- Result: Generic builds get closer to native performance

## CPU Feature Detection

Check what features your CPU supports:

```bash
# x86-64
lscpu | grep Flags

# ARM
lscpu | grep Features

# Check if binary uses specific features
objdump -d build/release/src/titan | grep -i avx2
objdump -d build/release/src/titan | grep -i neon
```

## Binary Analysis

### Check Dependencies

```bash
# Minimal dependencies (static build)
ldd build/release-generic-arm/src/titan
# Output:
#   linux-vdso.so.1
#   libc++.so.1
#   libc++abi.so.1
#   libc.so.6
#   libm.so.6
```

### Check Size

```bash
# Before strip
ls -lh build/release-generic-arm/src/titan
# 31MB

# Strip debug symbols
strip build/release-generic-arm/src/titan

# After strip
ls -lh build/release-generic-arm/src/titan
# 7.3MB (75% reduction!)
```

### Check Symbols

```bash
# Symbol count before strip
nm build/release-generic-arm/src/titan | wc -l
# 36,895 symbols

# Strip
strip build/release-generic-arm/src/titan

# Symbol count after strip
nm build/release-generic-arm/src/titan | wc -l
# 18,284 symbols (49% reduction)
```

## CI/CD Build Matrix

For GitHub Actions or similar:

```yaml
strategy:
  matrix:
    preset:
      - release-generic        # x86-64 (Docker/K8s)
      - release-generic-arm    # ARM64 (Graviton, Apple Silicon)
    include:
      - preset: release-generic
        os: ubuntu-latest
        arch: x86_64
      - preset: release-generic-arm
        os: ubuntu-latest
        arch: arm64

steps:
  - name: Build
    run: |
      cmake --preset=${{ matrix.preset }}
      cmake --build --preset=${{ matrix.preset }} --parallel $(nproc)
      strip build/${{ matrix.preset }}/src/titan

  - name: Upload artifact
    uses: actions/upload-artifact@v3
    with:
      name: titan-${{ matrix.arch }}
      path: build/${{ matrix.preset }}/src/titan
```

## Performance Testing

After building, test performance:

```bash
# Start backend
cd tests/mock-backend && python3 main.py &

# Start Titan
./build/release-generic-arm/src/titan --config config/benchmark.json &

# Benchmark HTTP/1.1
wrk -t4 -c100 -d30s http://localhost:8080/

# Benchmark HTTP/2
h2load -n 100000 -c 100 -t 4 https://localhost:8443/
```

## Troubleshooting

### Build Fails with "Illegal Instruction"

**Problem**: Binary built with `-march=native` on different CPU

**Solution**: Use generic build:
```bash
cmake --preset=release-generic-arm  # For ARM
cmake --preset=release-generic      # For x86-64
```

### Slow Build Time

**Problem**: Full LTO takes too long

**Solution**: Use thin LTO for development:
```bash
# Edit CMakePresets.json, change -flto=full to -flto=thin
cmake --preset=release
```

### Large Binary Size

**Solution**: Strip debug symbols:
```bash
strip build/release-generic-arm/src/titan
```

## Recommended Build Strategy

### For Local Development
```bash
cmake --preset=dev
cmake --build --preset=dev
```
Fast iteration, full debugging support.

### For Local Performance Testing
```bash
cmake --preset=release
cmake --build --preset=release
```
Maximum performance on your specific CPU.

### For Docker Images
```bash
cmake --preset=release-generic       # x86-64
# OR
cmake --preset=release-generic-arm   # ARM64

cmake --build --preset=<preset> --parallel $(nproc)
strip build/<preset>/src/titan
```
Optimal balance of performance and compatibility.

### For Bare Metal (systemd)
```bash
cmake --preset=static
cmake --build --preset=static
strip build/static/src/titan
```
Maximum performance with minimal dependencies.

## Build Cache

Speed up rebuilds with ccache:

```bash
# Install ccache
apt-get install ccache

# Configure CMake
export CMAKE_CXX_COMPILER_LAUNCHER=ccache

# Build (first time: 10 min, subsequent: 2 min)
cmake --preset=release-generic-arm
cmake --build --preset=release-generic-arm
```

## References

- [x86-64 Microarchitecture Levels](https://en.wikipedia.org/wiki/X86-64#Microarchitecture_levels)
- [Clang Optimization Flags](https://clang.llvm.org/docs/CommandGuide/clang.html#code-generation-options)
- [LTO Documentation](https://llvm.org/docs/LinkTimeOptimization.html)
