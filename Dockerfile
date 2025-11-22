# Titan API Gateway - Development & Build Environment
# Ubuntu 24.04 ARM64 with Clang 18, C++23 modules support

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV CC=clang-18
ENV CXX=clang++-18

# Install build essentials and dependencies
RUN apt-get update && apt-get install -y \
    # Compilers and build tools
    clang-18 \
    clang-tools-18 \
    libc++-18-dev \
    libc++abi-18-dev \
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
    perf \
    graphviz \
    # Python testing infrastructure
    python3 \
    python3-pip \
    python3-venv \
    # HTTP benchmarking tools
    wrk \
    apache2-utils \
    nghttp2-client \
    # Cleanup
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Install Python testing dependencies
COPY tests/integration/requirements.txt /tmp/integration-requirements.txt
COPY tests/mock-backend/requirements.txt /tmp/backend-requirements.txt
RUN pip3 install --no-cache-dir --break-system-packages \
    -r /tmp/integration-requirements.txt \
    -r /tmp/backend-requirements.txt \
    && rm /tmp/integration-requirements.txt /tmp/backend-requirements.txt

# Install FlameGraph for CPU profiling visualization
RUN git clone --depth=1 https://github.com/brendangregg/FlameGraph.git /opt/FlameGraph \
    && ln -s /opt/FlameGraph/flamegraph.pl /usr/local/bin/flamegraph.pl \
    && ln -s /opt/FlameGraph/stackcollapse-perf.pl /usr/local/bin/stackcollapse-perf.pl

# Install vcpkg
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git ${VCPKG_ROOT} \
    && ${VCPKG_ROOT}/bootstrap-vcpkg.sh -disableMetrics \
    && ln -s ${VCPKG_ROOT}/vcpkg /usr/local/bin/vcpkg

# Create working directory
WORKDIR /workspace

# Set default C++ standard
ENV CXXFLAGS="-std=c++23 -stdlib=libc++"

CMD ["/bin/bash"]
