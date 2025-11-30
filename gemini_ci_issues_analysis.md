Titan Gateway CI/CD Repair & Implementation GuideThis document details the configuration required to fix the Titan Gateway CI pipeline. It addresses all 8 requirements, including the transition to native ARM64 runners and the integration of security scanning.1. Workflow Configuration FileCreate or replace the file at .github/workflows/titan-ci.yml with the content below.Key Features of this Configuration:Native ARM64 Support: Uses ubuntu-24.04-arm for native compilation speed (no QEMU emulation).Fail-Fast Governance: Checks branch naming before provisioning expensive build runners.Hybrid Testing: Exports LD_LIBRARY_PATH so Python integration tests can load the compiled C++ binaries.Security: Implements CodeQL (SAST) and Trivy (dependency scanning).name: Titan Gateway CI

# Requirement 1: Automatic trigger on PR open, synchronize, reopened
on:
  pull_request:
    types: [opened, synchronize, reopened]
    branches: [main]
  push:
    branches: [main]

# Concurrency: Cancel obsolete builds on the same branch to save resources
concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true

jobs:
  # Requirement 2: Branch Validation (epic/, feat/, fix/, chore/)
  check-branch-name:
    name: Governance / Branch Name Check
    runs-on: ubuntu-latest
    steps:
      - name: Validate Branch Name
        run: |
          branch_name="${{ github.head_ref }}"
          echo "Verifying branch name: $branch_name"
          if [[ ! "$branch_name" =~ ^(epic|feat|fix|chore)/[a-zA-Z0-9_\-]+$ ]]; then
            echo "::error::Invalid branch name '$branch_name'. Must start with epic/, feat/, fix/, or chore/ followed by descriptive text."
            exit 1
          fi
          echo "Branch name is valid."

  # Requirements 3, 4, 5, 6: Build, Test, and Quality
  build-and-test:
    name: Build & Test (${{ matrix.arch }})
    needs: check-branch-name
    strategy:
      fail-fast: false
      matrix:
        include:
          # Requirement 3: Multi-platform build
          - arch: x86_64
            runner: ubuntu-24.04
            build_type: Release
          - arch: ARM64
            runner: ubuntu-24.04-arm # Uses GitHub Native ARM Runners
            build_type: Release

    runs-on: ${{ matrix.runner }}

    steps:
      - name: Checkout Code
        uses: actions/checkout@v4
        with:
          submodules: recursive # Critical for C++ deps

      - name: Install System Dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake build-essential python3 python3-pip ninja-build libssl-dev clang-tidy clang-format

      - name: Setup Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.10'

      - name: Install Python Deps
        run: |
          if [ -f requirements.txt ]; then pip install -r requirements.txt; fi
          pip install pytest

      # Requirement 6: Code Quality (Clang-Format) - x86 only to save time
      - name: Run Clang-Format Check
        if: matrix.arch == 'x86_64'
        run: |
          find src include -name '*.cpp' -o -name '*.h' | xargs clang-format -style=file --dry-run --Werror

      # Configure CMake with Compile Commands for Clang-Tidy
      - name: Configure CMake
        run: |
          cmake -B build \
            -DCMAKE_BUILD_TYPE=${{ matrix.build_type }} \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            -GNinja

      # Requirement 6: Code Quality (Clang-Tidy)
      - name: Run Clang-Tidy
        if: matrix.arch == 'x86_64'
        run: |
          find src -name "*.cpp" | xargs clang-tidy -p build --warnings-as-errors=*

      # Requirement 3: Build
      - name: Build Project
        run: cmake --build build --config ${{ matrix.build_type }}

      # Requirement 4: Unit Tests (Catch2)
      - name: Run Unit Tests (Catch2)
        working-directory: build
        run: ctest --output-on-failure -C ${{ matrix.build_type }} --parallel $(nproc)

      # Requirement 5: Integration Tests (Python)
      - name: Run Integration Tests (Python)
        run: |
          if [ -d "tests/integration" ]; then
             echo "Integration tests found."
             # Critical Fix: Allow Python to find compiled C++ libs
             export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${{ github.workspace }}/build
             pytest tests/integration/
          else
             echo "No integration tests found. Skipping."
          fi

  # Requirement 7: Security Scanning
  security-scan:
    name: Security / Vulnerability Scanning
    runs-on: ubuntu-latest
    permissions:
      security-events: write
      actions: read
      contents: read

    steps:
      - name: Checkout Code
        uses: actions/checkout@v4

      - name: Run Trivy Vulnerability Scanner
        uses: aquasecurity/trivy-action@master
        with:
          scan-type: 'fs'
          ignore-unfixed: true
          format: 'table'
          exit-code: '1'
          severity: 'CRITICAL,HIGH'

      - name: Initialize CodeQL
        uses: github/codeql-action/init@v3
        with:
          languages: cpp

      # Manual build for CodeQL is often more reliable for C++
      - name: Build for CodeQL
        run: |
           sudo apt-get update && sudo apt-get install -y cmake ninja-build libssl-dev
           cmake -B build -DCMAKE_BUILD_TYPE=Release
           cmake --build build

      - name: Perform CodeQL Analysis
        uses: github/codeql-action/analyze@v3
2. Mandatory Repository Settings (Requirement 8)The "PR status checks" requirement cannot be fulfilled by the YAML file alone. You must enforce this in the GitHub Repository Settings.Navigate to Settings > Code and automation > Branches.Click Add branch ruleset (or edit the protection rule for main).Set Target branches to main.Enable Require status checks to pass before merging.Search for and add the following status checks (these names match the YAML name fields):Governance / Branch Name CheckBuild & Test (x86_64)Build & Test (ARM64)Security / Vulnerability Scanning3. Project PrerequisitesTo ensure the CI passes locally and remotely, ensure your project structure supports the tools invoked:CMake Testing: Your CMakeLists.txt must contain:enable_testing()
# Example for Catch2
add_test(NAME TitanCoreTests COMMAND test_executable)
Linting Config: Ensure .clang-format and .clang-tidy files exist in the project root.Python Deps: If integration tests exist, ensure a requirements.txt is present or the dependencies are minimal.
