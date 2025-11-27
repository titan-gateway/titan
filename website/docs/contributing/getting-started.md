---
sidebar_position: 1
title: Contributing Guide
---

# Contributing to Titan

Thank you for your interest in contributing to Titan! This guide covers the essentials for getting started. For complete setup instructions, see the [CONTRIBUTING.md](https://github.com/JonathanBerhe/titan/blob/main/CONTRIBUTING.md) file in the repository.

:::info Code of Conduct
This project is released with a [Contributor Code of Conduct](https://github.com/JonathanBerhe/titan/blob/main/CODE_OF_CONDUCT.md). By participating, you agree to abide by its terms.
:::

## Quick Start

```bash
# Clone the repository
git clone https://github.com/JonathanBerhe/titan.git
cd titan

# Quick setup for development
make dev

# Run tests
make test

# Format code
make format
```

## Branch Naming Convention

Branch names **must** follow this pattern to enable automated versioning:

| Prefix | Version Bump | Example | Use Case |
|--------|--------------|---------|----------|
| `epic/` | **MAJOR** (v0.x.x → v1.0.0) | `epic/new-architecture` | Breaking changes, major rewrites |
| `feat/` | **MINOR** (v0.1.x → v0.2.0) | `feat/connection-pooling` | New features, backward compatible |
| `fix/` | **PATCH** (v0.1.0 → v0.1.1) | `fix/memory-leak` | Bug fixes, security patches |
| `chore/` | **No bump** | `chore/update-deps` | Dependencies, refactoring, docs |

### Examples

```bash
# Creating a new feature (minor version bump)
git checkout -b feat/http2-support main
# ... make changes ...
git push origin feat/http2-support
# Create PR → After merge → automatic release

# Creating a bug fix (patch version bump)
git checkout -b fix/segfault-in-parser main
# ... make changes ...
git push origin fix/segfault-in-parser
```

## Pull Request Process

1. **Create a feature branch** with the proper prefix
2. **Make your changes** and commit with descriptive messages
3. **Run all CI checks locally:**
   ```bash
   make ci
   ```
4. **Push your branch:**
   ```bash
   git push origin feat/your-feature
   ```
5. **Create Pull Request** on GitHub
6. **Wait for CI checks** to pass (build, test, lint, security scan)
7. **Request review** from maintainers

### CI Checks

All PRs must pass:

- Branch name validation
- Build on x86_64 and ARM64
- Unit tests
- Code formatting (clang-format)
- Static analysis (clang-tidy)
- Security scan (CodeQL, Trivy)

### After Merge

Once your PR is merged to `main`:
1. GitHub Actions automatically calculates the version bump based on your branch prefix
2. Creates a new git tag (e.g., `v0.2.0`)
3. Builds and publishes release artifacts:
   - Multi-architecture binaries
   - Docker images to `ghcr.io`
   - Helm charts

## Coding Standards

- **Modern C++** with Google C++ Style
- **Line length:** 100 characters
- **Indentation:** 4 spaces (no tabs)
- **Formatting:** Use `make format` before committing
- **Testing:** Write unit tests for new features
- **Documentation:** Update docs for user-facing changes

### Naming Conventions

- **Classes/Structs:** `CamelCase`
- **Functions:** `snake_case`
- **Variables:** `snake_case`
- **Constants:** `UPPER_CASE`
- **Namespaces:** `snake_case`

## Development Workflow

### Local Testing

```bash
# Full development setup
make dev

# Run unit tests
make test

# Run with verbose output
make test-verbose

# Run benchmarks
make benchmark
```

### Code Quality

```bash
# Format all C++ files
make format

# Check formatting (CI mode)
make format-check

# Run all linting checks
make lint

# Run all CI checks locally
make ci
```

## Getting Help

- **Documentation:** Check the `/docs` folder or this website
- **Issues:** [Open an issue](https://github.com/JonathanBerhe/titan/issues) on GitHub
- **Discussions:** Use [GitHub Discussions](https://github.com/JonathanBerhe/titan/discussions) for questions

## License

By contributing, you agree that your contributions will be licensed under the [Apache License 2.0](https://github.com/JonathanBerhe/titan/blob/main/LICENSE).
