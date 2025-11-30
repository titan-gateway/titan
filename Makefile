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
		2>/dev/null || (echo "Coverage generation failed (lcov not installed?)" && exit 1)
	@echo ""
	@echo "Coverage Summary:"
	@lcov --list coverage.info 2>/dev/null || true
	@echo ""
	@echo "Coverage report generated: coverage.info"
	@echo "To view HTML report, run: genhtml coverage.info --output-directory coverage_html && open coverage_html/index.html"

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
