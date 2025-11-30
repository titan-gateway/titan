# Titan API Gateway - Development & Build Environment
# Ubuntu 24.04 ARM64 with GCC 14, libstdc++, C++23 support

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV CC=gcc-14
ENV CXX=g++-14

# Install build essentials and dependencies
RUN apt-get update && apt-get install -y \
    software-properties-common \
    wget \
    && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && apt-get update && apt-get install -y \
    # Compilers and build tools
    gcc-14 \
    g++-14 \
    libstdc++-14-dev \
    clang-18 \
    clang-tools-18 \
    lld-18 \
    cmake \
    ninja-build \
    git \
    curl \
    pkg-config \
    # vcpkg dependencies
    zip \
    unzip \
    tar \
    # OpenSSL
    libssl-dev \
    # pthread
    libpthread-stubs0-dev \
    # Development tools
    gdb \
    valgrind \
    linux-tools-generic \
    linux-tools-common \
    strace \
    # Profiling and analysis
    graphviz \
    # Python testing infrastructure
    python3 \
    python3-pip \
    python3-venv \
    # HTTP benchmarking tools
    wrk \
    apache2-utils \
    nghttp2-client \
    # Proxy servers for benchmarking
    nginx \
    haproxy \
    # Additional dependencies for Envoy
    wget \
    ca-certificates \
    # Benchmark utilities
    bc \
    lsof \
    # Cleanup
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Install Python testing dependencies (if they exist)
RUN pip3 install --no-cache-dir --break-system-packages \
    requests pytest fastapi uvicorn || true

# Install Python packages for benchmark comparison
RUN pip3 install --no-cache-dir --break-system-packages \
    tabulate \
    matplotlib \
    psutil

# Install FlameGraph for CPU profiling visualization
RUN git clone --depth=1 https://github.com/brendangregg/FlameGraph.git /opt/FlameGraph \
    && ln -s /opt/FlameGraph/flamegraph.pl /usr/local/bin/flamegraph.pl \
    && ln -s /opt/FlameGraph/stackcollapse-perf.pl /usr/local/bin/stackcollapse-perf.pl

# Install vcpkg
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git ${VCPKG_ROOT} \
    && ${VCPKG_ROOT}/bootstrap-vcpkg.sh -disableMetrics \
    && ln -s ${VCPKG_ROOT}/vcpkg /usr/local/bin/vcpkg

# Install Envoy proxy (download binary from GitHub)
ENV ENVOY_VERSION=1.28.0
RUN ARCH="$(uname -m)" \
    && if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then \
        ENVOY_ARCH="aarch_64"; \
    else \
        ENVOY_ARCH="x86_64"; \
    fi \
    && wget -q "https://github.com/envoyproxy/envoy/releases/download/v${ENVOY_VERSION}/envoy-${ENVOY_VERSION}-linux-${ENVOY_ARCH}" \
        -O /usr/local/bin/envoy \
    && chmod +x /usr/local/bin/envoy

# Create working directory
WORKDIR /workspace

CMD ["/bin/bash"]
