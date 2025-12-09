# Titan API Gateway - Makefile
# Convenient commands for development, building, and testing

.PHONY: help configure build clean test format lint install run all

# Default target
.DEFAULT_GOAL := help

# Build presets
PRESET ?= dev
BUILD_DIR = build/$(PRESET)

##@ Help

help: ## Display this help message
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make \033[36m<target>\033[0m\n"} /^[a-zA-Z_-]+:.*?##/ { printf "  \033[36m%-15s\033[0m %s\n", $$1, $$2 } /^##@/ { printf "\n\033[1m%s\033[0m\n", substr($$0, 5) } ' $(MAKEFILE_LIST)

##@ Configuration & Building

configure: ## Configure CMake project (PRESET=dev|release)
	@echo "Configuring project with preset: $(PRESET)..."
	cmake --preset=$(PRESET)

build: ## Build the project (PRESET=dev|release)
	@echo "Building project with preset: $(PRESET)..."
	cmake --build --preset=$(PRESET) --parallel $$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

clean: ## Clean build artifacts
	@echo "Cleaning build directory..."
	rm -rf build/

rebuild: clean configure build ## Clean and rebuild from scratch

##@ Testing

test: ## Run unit tests
	@echo "Running unit tests..."
	cd $(BUILD_DIR) && ctest --output-on-failure --parallel $$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

test-verbose: ## Run unit tests with verbose output
	@echo "Running unit tests (verbose)..."
	cd $(BUILD_DIR) && ctest --output-on-failure --verbose

test-coverage: ## Run tests and generate coverage report
	@echo "Running tests with coverage..."
	@echo "Configuring with coverage flags..."
	cmake --preset=dev \
		-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer --coverage" \
		-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined --coverage"
	@echo "Building..."
	cmake --build --preset=dev --parallel $$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
	@echo "Running tests..."
	cd $(BUILD_DIR) && ctest --output-on-failure --parallel $$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
	@echo "Generating coverage report..."
	@lcov --capture --directory $(BUILD_DIR) \
		--output-file coverage.info \
		--exclude '/usr/*' \
		--exclude '/opt/*' \
		--exclude '*/vcpkg_installed/*' \
		--exclude '*/tests/*' \
		--exclude '*/build/*' \
		|| echo "Coverage generation skipped (no coverage data found)"
	@echo ""
	@if [ -f coverage.info ]; then \
		echo "Coverage Summary:"; \
		lcov --list coverage.info || true; \
		echo ""; \
		echo "Coverage report generated: coverage.info"; \
		echo "To view HTML report, run: genhtml coverage.info --output-directory coverage_html && open coverage_html/index.html"; \
	fi

benchmark: ## Run benchmarks
	@echo "Running benchmarks..."
	cd benchmarks && ./run-benchmarks.sh

##@ Code Quality

# Detect clang-format version (prefer 21, fallback to default)
CLANG_FORMAT := $(shell command -v clang-format-21 2>/dev/null || command -v clang-format 2>/dev/null)

format: ## Format all C++ source files with clang-format-21
	@echo "Formatting C++ files..."
	@if [ -z "$(CLANG_FORMAT)" ]; then \
		echo "Error: clang-format not found"; \
		echo "Install: brew install clang-format (macOS) or see Dockerfile (Linux)"; \
		exit 1; \
	fi
	@echo "Using: $(CLANG_FORMAT)"
	@find src tests -type f \( -name "*.cpp" -o -name "*.hpp" \) -exec $(CLANG_FORMAT) -i {} \;
	@echo "✅ All C++ files formatted"

format-check: ## Check if files are properly formatted (CI)
	@echo "Checking code formatting..."
	@if [ -z "$(CLANG_FORMAT)" ]; then \
		echo "Error: clang-format not found"; \
		exit 1; \
	fi
	@echo "Using: $(CLANG_FORMAT)"
	@find src tests -type f \( -name "*.cpp" -o -name "*.hpp" \) -exec $(CLANG_FORMAT) --dry-run --Werror {} \;
	@echo "✅ All files are properly formatted"

lint: ## Run all linting checks
	@echo "Running lint checks..."
	@./scripts/lint.sh

lint-all: ## Run all linting checks including website
	@echo "Running all lint checks..."
	@./scripts/lint.sh --all

##@ Website (Documentation)

website-install: ## Install website dependencies
	@echo "Installing website dependencies..."
	cd website && npm install

website-dev: ## Start website development server
	@echo "Starting website dev server..."
	cd website && npm start

website-build: ## Build website for production
	@echo "Building website..."
	cd website && npm run build

website-format: ## Format website code with Prettier
	@echo "Formatting website code..."
	cd website && npm run format

website-lint: ## Lint website code with ESLint
	@echo "Linting website code..."
	cd website && npm run lint

##@ Pre-commit Hooks

hooks-install: ## Install pre-commit hooks
	@echo "Installing pre-commit hooks..."
	@if ! command -v pre-commit >/dev/null 2>&1; then \
		echo "Installing pre-commit..."; \
		pip install pre-commit; \
	fi
	pre-commit install
	@echo "✅ Pre-commit hooks installed"

hooks-run: ## Run pre-commit hooks on all files
	@echo "Running pre-commit hooks..."
	pre-commit run --all-files

##@ Running

run: ## Run Titan with default config
	@echo "Running Titan..."
	$(BUILD_DIR)/src/titan --config config/test.json

run-benchmark: ## Run Titan with benchmark config
	@echo "Running Titan (benchmark mode)..."
	$(BUILD_DIR)/src/titan --config config/benchmark.json

##@ Installation

install: ## Install Titan binary to /usr/local/bin
	@echo "Installing Titan..."
	sudo cp $(BUILD_DIR)/src/titan /usr/local/bin/
	@echo "✅ Titan installed to /usr/local/bin/titan"

uninstall: ## Uninstall Titan from /usr/local/bin
	@echo "Uninstalling Titan..."
	sudo rm -f /usr/local/bin/titan
	@echo "✅ Titan uninstalled"

##@ Docker

docker-build: ## Build Docker image
	@echo "Building Docker image..."
	docker build -f Dockerfile.production -t titan:latest .

docker-run: ## Run Titan in Docker
	@echo "Running Titan in Docker..."
	docker run -p 8080:8080 -v $$(pwd)/config:/etc/titan titan:latest

##@ Complete Workflows

all: configure build test ## Configure, build, and test (full CI workflow)
	@echo "✅ All checks passed!"

dev: configure build ## Quick setup for development
	@echo "✅ Development environment ready"
	@echo ""
	@echo "Next steps:"
	@echo "  - Run: make run"
	@echo "  - Test: make test"
	@echo "  - Format: make format"

ci: format-check lint test ## Run all CI checks locally
	@echo "✅ All CI checks passed!"

release: PRESET=release
release: clean configure build test ## Build optimized release binary
	@echo "✅ Release build complete: $(BUILD_DIR)/src/titan"
	@ls -lh $(BUILD_DIR)/src/titan

##@ Profiling & Benchmarking

# Profiling directories
PROFILE_DIR := profiling
RESULTS_DIR := results
FLAMEGRAPH_DIR := tools/flamegraph

# Benchmark parameters
BENCH_DURATION ?= 30
BENCH_CONNECTIONS ?= 100
BENCH_THREADS ?= 4
BENCH_HOST ?= http://localhost:8080
BENCH_PATH ?= /api

# Backend server
BACKEND_PORT ?= 3001

check-tools: ## Check for required profiling/benchmarking tools
	@./scripts/check_tools.sh

profile-cpu: ## CPU profiling with gperftools (PRESET=release)
	@echo "==> CPU Profiling with gperftools..."
	@mkdir -p $(PROFILE_DIR)
	@./scripts/check_tools.sh pprof
	@echo "Building with profiling enabled..."
	@PRESET=release make build
	@echo "Starting Titan with CPU profiling..."
	@CPUPROFILE=$(PROFILE_DIR)/cpu.prof $(BUILD_DIR)/src/titan --config config/benchmark-http1.json &
	@TITAN_PID=$$!; \
	echo "Titan PID: $$TITAN_PID"; \
	sleep 2; \
	echo "Generating load for $(BENCH_DURATION) seconds..."; \
	wrk -t$(BENCH_THREADS) -c$(BENCH_CONNECTIONS) -d$(BENCH_DURATION)s $(BENCH_HOST)$(BENCH_PATH) || true; \
	echo "Stopping Titan..."; \
	kill $$TITAN_PID 2>/dev/null || true; \
	wait $$TITAN_PID 2>/dev/null || true
	@echo "✅ CPU profile saved to $(PROFILE_DIR)/cpu.prof"
	@echo "Analyze with: pprof --text $(BUILD_DIR)/src/titan $(PROFILE_DIR)/cpu.prof"
	@echo "Visualize with: pprof --web $(BUILD_DIR)/src/titan $(PROFILE_DIR)/cpu.prof"

profile-cpu-perf: ## CPU profiling with perf + flamegraph (Linux only)
	@echo "==> CPU Profiling with perf + flamegraph..."
	@mkdir -p $(PROFILE_DIR)
	@./scripts/check_tools.sh perf
	@./scripts/generate_flamegraph.sh $(BENCH_DURATION) $(BENCH_CONNECTIONS) $(BENCH_THREADS)
	@echo "✅ Flamegraph saved to $(PROFILE_DIR)/flamegraph.svg"
	@echo "Open with: open $(PROFILE_DIR)/flamegraph.svg"

profile-heap: ## Heap profiling with gperftools
	@echo "==> Heap Profiling with gperftools..."
	@mkdir -p $(PROFILE_DIR)
	@./scripts/check_tools.sh pprof
	@echo "Building with profiling enabled..."
	@PRESET=release make build
	@echo "Starting Titan with heap profiling..."
	@HEAPPROFILE=$(PROFILE_DIR)/heap.prof $(BUILD_DIR)/src/titan --config config/benchmark-http1.json &
	@TITAN_PID=$$!; \
	echo "Titan PID: $$TITAN_PID"; \
	sleep 2; \
	echo "Generating load for $(BENCH_DURATION) seconds..."; \
	wrk -t$(BENCH_THREADS) -c$(BENCH_CONNECTIONS) -d$(BENCH_DURATION)s $(BENCH_HOST)$(BENCH_PATH) || true; \
	echo "Stopping Titan..."; \
	kill $$TITAN_PID 2>/dev/null || true; \
	wait $$TITAN_PID 2>/dev/null || true
	@echo "✅ Heap profile saved to $(PROFILE_DIR)/heap.prof.*"
	@echo "Analyze with: pprof --text $(BUILD_DIR)/src/titan $(PROFILE_DIR)/heap.prof.*"

analyze-profiles: ## Analyze and generate reports from profiles
	@echo "==> Analyzing profiles..."
	@if [ -f $(PROFILE_DIR)/cpu.prof ]; then \
		echo "CPU Profile Summary:"; \
		pprof --text $(BUILD_DIR)/src/titan $(PROFILE_DIR)/cpu.prof | head -20; \
	fi
	@if ls $(PROFILE_DIR)/heap.prof.* 1>/dev/null 2>&1; then \
		echo ""; \
		echo "Heap Profile Summary:"; \
		pprof --text $(BUILD_DIR)/src/titan $$(ls -t $(PROFILE_DIR)/heap.prof.* | head -1) | head -20; \
	fi

bench-http1: ## Benchmark HTTP/1.1 cleartext (baseline)
	@echo "==> Benchmarking HTTP/1.1 Cleartext..."
	@mkdir -p $(RESULTS_DIR)
	@./scripts/benchmark_runner.py \
		--scenario http1 \
		--config config/benchmark-http1.json \
		--duration $(BENCH_DURATION) \
		--connections $(BENCH_CONNECTIONS) \
		--threads $(BENCH_THREADS) \
		--output $(RESULTS_DIR)/bench-http1.json

bench-http2-tls: ## Benchmark HTTP/2 with TLS (production scenario)
	@echo "==> Benchmarking HTTP/2 with TLS..."
	@mkdir -p $(RESULTS_DIR)
	@./scripts/benchmark_runner.py \
		--scenario http2-tls \
		--config config/benchmark-http2-tls.json \
		--duration $(BENCH_DURATION) \
		--connections $(BENCH_CONNECTIONS) \
		--threads $(BENCH_THREADS) \
		--output $(RESULTS_DIR)/bench-http2-tls.json

bench-jwt: ## Benchmark with JWT authentication
	@echo "==> Benchmarking with JWT Authentication..."
	@mkdir -p $(RESULTS_DIR)
	@./scripts/benchmark_runner.py \
		--scenario jwt \
		--config config/benchmark-jwt.json \
		--duration $(BENCH_DURATION) \
		--connections $(BENCH_CONNECTIONS) \
		--threads $(BENCH_THREADS) \
		--output $(RESULTS_DIR)/bench-jwt.json

bench-pool: ## Benchmark connection pool stress test
	@echo "==> Benchmarking Connection Pool..."
	@mkdir -p $(RESULTS_DIR)
	@./scripts/benchmark_runner.py \
		--scenario pool \
		--config config/benchmark-pool.json \
		--duration $(BENCH_DURATION) \
		--connections $(BENCH_CONNECTIONS) \
		--threads $(BENCH_THREADS) \
		--output $(RESULTS_DIR)/bench-pool.json

bench-middleware-none: ## Benchmark with zero middleware (raw proxy)
	@echo "==> Benchmarking with Zero Middleware..."
	@mkdir -p $(RESULTS_DIR)
	@./scripts/benchmark_runner.py \
		--scenario middleware-none \
		--config config/benchmark-middleware-none.json \
		--duration $(BENCH_DURATION) \
		--connections $(BENCH_CONNECTIONS) \
		--threads $(BENCH_THREADS) \
		--output $(RESULTS_DIR)/bench-middleware-none.json

bench-middleware-all: ## Benchmark with all middleware enabled
	@echo "==> Benchmarking with All Middleware..."
	@mkdir -p $(RESULTS_DIR)
	@./scripts/benchmark_runner.py \
		--scenario middleware-all \
		--config config/benchmark-middleware-all.json \
		--duration $(BENCH_DURATION) \
		--connections $(BENCH_CONNECTIONS) \
		--threads $(BENCH_THREADS) \
		--output $(RESULTS_DIR)/bench-middleware-all.json

bench-all: ## Run all benchmark scenarios
	@echo "==> Running All Benchmark Scenarios..."
	@make bench-http1 BENCH_DURATION=$(BENCH_DURATION)
	@make bench-http2-tls BENCH_DURATION=$(BENCH_DURATION)
	@make bench-jwt BENCH_DURATION=$(BENCH_DURATION)
	@make bench-pool BENCH_DURATION=$(BENCH_DURATION)
	@make bench-middleware-none BENCH_DURATION=$(BENCH_DURATION)
	@make bench-middleware-all BENCH_DURATION=$(BENCH_DURATION)
	@echo "✅ All benchmarks complete! Results in $(RESULTS_DIR)/"
	@echo "Generate report with: make bench-report"

bench-compare: ## Compare benchmark results (BEFORE=file1.json AFTER=file2.json)
	@if [ -z "$(BEFORE)" ] || [ -z "$(AFTER)" ]; then \
		echo "Error: Please specify BEFORE and AFTER files"; \
		echo "Usage: make bench-compare BEFORE=results/before.json AFTER=results/after.json"; \
		exit 1; \
	fi
	@./scripts/compare_results.py $(BEFORE) $(AFTER)

bench-report: ## Generate comprehensive benchmark report
	@echo "==> Generating Benchmark Report..."
	@./scripts/benchmark_runner.py --report $(RESULTS_DIR)

setup-backend: ## Start mock backend for benchmarking
	@echo "==> Starting mock backend on port $(BACKEND_PORT)..."
	@cd tests/mock-backend && python3 main.py $(BACKEND_PORT) &
	@echo "✅ Backend started on http://localhost:$(BACKEND_PORT)"

clean-profiles: ## Clean profiling and benchmark data
	@echo "Cleaning profiling data..."
	@rm -rf $(PROFILE_DIR)/ $(RESULTS_DIR)/
	@echo "✅ Profiling data cleaned"
