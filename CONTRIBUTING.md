# Contributing to Titan API Gateway

Thank you for your interest in contributing to Titan! This guide will help you set up your development environment and follow our coding standards.

Please note that this project is released with a [Contributor Code of Conduct](CODE_OF_CONDUCT.md). By participating in this project you agree to abide by its terms.

## Table of Contents

- [Development Setup](#development-setup)
- [Code Style & Linting](#code-style--linting)
- [Building & Testing](#building--testing)
- [Submitting Changes](#submitting-changes)

## Development Setup

### Prerequisites

- **C++ Compiler:** Clang 18+ or GCC 14+
- **CMake:** 3.28+
- **Node.js:** 20+ (for website)
- **Python:** 3.8+ (for tests)
- **vcpkg:** For C++ dependencies

### Clone and Build

```bash
git clone https://github.com/JonathanBerhe/titan.git
cd titan

# Quick setup for development
make dev

# Run tests
make test
```

### Install Linting Tools

#### C++ Tools (clang-format, clang-tidy)

**macOS:**
```bash
brew install llvm
```

**Ubuntu/Debian:**
```bash
sudo apt install clang-format-18 clang-tidy-18
```

#### Pre-commit Hooks (Recommended)

```bash
make hooks-install
```

This will automatically format and lint your code before each commit.

#### Website Tools (ESLint, Prettier)

```bash
make website-install
```

## Code Style & Linting

### C++ Code

#### Format Code

```bash
# Format all C++ files
make format

# Check formatting (CI mode)
make format-check
```

#### Lint Code

```bash
# Run all linting checks
make lint

# Run with website checks
make lint-all
```

#### Style Guidelines

- **Standard:** C++23
- **Line length:** 100 characters
- **Indentation:** 4 spaces (no tabs)
- **Naming:**
  - Classes/Structs: `CamelCase`
  - Functions: `snake_case`
  - Variables: `snake_case`
  - Constants: `UPPER_CASE`
  - Namespaces: `snake_case`

### Website Code (TypeScript/React)

#### Format & Lint

```bash
# Format with Prettier
make website-format

# Lint with ESLint
make website-lint

# Or use npm directly
cd website && npm run lint:fix
```

### Markdown Documentation

All documentation follows Markdown linting rules. Pre-commit hooks will automatically fix formatting issues.

## Building & Testing

### Build Configurations

```bash
# Development build (with debug symbols)
make dev

# Release build (optimized)
make release

# Clean and rebuild
make rebuild
```

### Running Tests

```bash
# Unit tests
make test

# Unit tests (verbose)
make test-verbose

# Run Titan
make run

# Benchmarks
make benchmark
```

## Submitting Changes

### Branch Naming Convention

Branch names **must** follow this pattern:

- `epic/description` - Breaking changes (major version bump)
- `feat/description` - New features (minor version bump)
- `fix/description` - Bug fixes (patch version bump)
- `chore/description` - Maintenance tasks (no version bump)

**Examples:**
```bash
git checkout -b feat/connection-pooling
git checkout -b fix/memory-leak
git checkout -b chore/update-deps
```

### Commit Messages

Write clear, descriptive commit messages:

```
feat: add HTTP/2 connection pooling

- Implement per-upstream connection pools
- Add health checks for pooled connections
- Update documentation
```

### Pull Request Process

1. **Create a feature branch** with proper prefix
2. **Make your changes** and commit
3. **Run all CI checks locally:**
   ```bash
   make ci
   ```
4. **Push your branch:**
   ```bash
   git push origin feat/your-feature
   ```
5. **Create Pull Request** on GitHub
6. **Wait for CI checks** to pass
7. **Request review** from maintainers

### CI Checks

All PRs must pass:

- ✅ Branch name validation
- ✅ Build on x86_64 and ARM64
- ✅ Unit tests
- ✅ Code formatting (clang-format)
- ✅ Static analysis (clang-tidy)
- ✅ Security scan (CodeQL, Trivy)

## VS Code Setup

We recommend using VS Code with these extensions (see `.vscode/extensions.json`):

- C/C++ (Microsoft)
- CMake Tools
- Clang-Format
- ESLint
- Prettier

Settings are pre-configured in `.vscode/settings.json` for:
- Auto-format on save
- Correct indentation for C++ (4 spaces) and TypeScript (2 spaces)
- IntelliSense with C++23 support

## Questions?

- **Documentation:** Check the `/docs` folder
- **Issues:** Open an issue on GitHub
- **Discussions:** Use GitHub Discussions for questions

## License

By contributing, you agree that your contributions will be licensed under the Apache License 2.0.
