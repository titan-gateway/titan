# Branch Naming Guide

Quick reference for branch naming conventions and versioning strategy.

## ✅ Valid Branch Prefixes

| Prefix | Version Bump | Example Branch | Result |
|--------|--------------|----------------|--------|
| `epic/` | MAJOR (x.0.0) | `epic/new-architecture` | v0.5.2 → **v1.0.0** |
| `feat/` | MINOR (0.x.0) | `feat/http2-support` | v0.5.2 → **v0.6.0** |
| `fix/` | PATCH (0.0.x) | `fix/memory-leak` | v0.5.2 → **v0.5.3** |
| `chore/` | No bump | `chore/update-deps` | v0.5.2 → **v0.5.2** (no release) |

## 📝 Naming Examples

### ✅ Good Examples
```
epic/microservices-architecture
epic/breaking-api-v2
feat/connection-pooling
feat/add-prometheus-metrics
fix/segfault-in-parser
fix/cors-headers-bug
chore/update-cmake-version
chore/refactor-tests
```

### ❌ Bad Examples
```
my-feature              ❌ No prefix
feature/new-thing       ❌ Should be "feat/" not "feature/"
bugfix/issue-123        ❌ Should be "fix/" not "bugfix/"
epic-new-ui             ❌ Use "/" not "-"
FEAT/uppercase          ❌ Use lowercase
```

## 🚀 Quick Workflow

1. Create branch with correct prefix:
   ```bash
   git checkout -b feat/my-feature main
   ```

2. Push and create PR:
   ```bash
   git push origin feat/my-feature
   ```

3. After PR is merged to `main`:
   - ✅ Automatic version bump (feat/ → minor)
   - ✅ Git tag created (e.g., v0.6.0)
   - ✅ Binaries, Docker, and Helm released

## 💡 Tips

- **Breaking changes?** Use `epic/` prefix
- **New feature?** Use `feat/` prefix
- **Bug fix?** Use `fix/` prefix
- **No release needed?** Use `chore/` prefix

## 🔗 More Info

See [docs/CI_CD.md](../docs/CI_CD.md) for complete documentation.
