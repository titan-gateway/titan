# CI/CD Incremental Improvement Plan

## Current Status (2025-11-29)
✅ **CRITICAL FIX COMPLETED**: Pull request events now trigger CI workflows
- PR #19 successfully triggers on `pull_request` events
- Simplified ci.yml (59 lines) is working
- Branch validation: ✅ PASS
- Build & Test: ⚠️ Fails (dependency issues, not trigger issues)

## Root Cause Analysis
**Original Problem**: 244-line ci.yml was silently rejected by GitHub Actions
**Solution**: Simplified to minimal working version
**Proof**: test-pr.yml (15 lines) worked immediately

## Incremental Enhancement Plan

### Phase 1: Fix Current Build Failures (IMMEDIATE - Next Session)
**Goal**: Get the working CI to pass all checks

**Tasks**:
1. Fix vcpkg/dependency installation in Build & Test job
   - Ensure vcpkg is properly cached
   - Verify all C++ dependencies are available

2. Verify unit tests run successfully
   - Check that Catch2 tests execute
   - Ensure all 139 tests pass

**Files to Modify**:
- `.github/workflows/ci.yml` (build-test job only)

**Success Criteria**:
- PR #19 shows all green checkmarks
- Can merge PR #19 to main

---

### Phase 2: Add Gemini's Safe Improvements (1-2 PRs)
**Goal**: Enhance without breaking the trigger

**Task 2.1 - Better Concurrency (SAFE)**:
```yaml
concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true
```

**Task 2.2 - Stricter Branch Validation**:
```bash
if [[ ! "$branch_name" =~ ^(epic|feat|fix|chore)/[a-zA-Z0-9_\-]+$ ]]
```

**Task 2.3 - Add Integration Tests**:
```yaml
- name: Run Integration Tests
  run: |
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${{ github.workspace }}/build
    cd tests/integration
    pip install -r requirements.txt
    pytest -v --tb=short
```

**Files to Modify**:
- `.github/workflows/ci.yml`

**Success Criteria**:
- CI still triggers on PRs
- All jobs pass
- Integration tests run successfully

---

### Phase 3: Add ARM64 Native Builds (1 PR)
**Goal**: Multi-platform testing with native ARM runners

**Task 3.1 - Add Matrix Strategy**:
```yaml
build-and-test:
  strategy:
    fail-fast: false
    matrix:
      include:
        - arch: x86_64
          runner: ubuntu-22.04
        - arch: ARM64
          runner: ubuntu-24.04-arm  # GitHub Native ARM
```

**Task 3.2 - Verify BuildJet Pricing**:
- Check if ubuntu-24.04-arm is available
- Alternative: buildjet-4vcpu-ubuntu-2204-arm

**Files to Modify**:
- `.github/workflows/ci.yml` (add matrix to build-test job)

**Success Criteria**:
- Both x86_64 and ARM64 builds complete
- Both architectures run tests successfully

---

### Phase 4: Add Security Scanning (1 PR)
**Goal**: CodeQL and Trivy vulnerability scanning

**Task 4.1 - Add Security Job**:
```yaml
security-scan:
  name: Security Scan
  runs-on: ubuntu-latest
  permissions:
    security-events: write
    actions: read
    contents: read
  steps:
    - uses: actions/checkout@v4

    - name: Run Trivy
      uses: aquasecurity/trivy-action@master
      with:
        scan-type: 'fs'
        severity: 'CRITICAL,HIGH'
        exit-code: '1'

    - name: Initialize CodeQL
      uses: github/codeql-action/init@v3
      with:
        languages: cpp

    - name: Build for CodeQL
      run: |
        make configure PRESET=dev
        make build PRESET=dev

    - name: CodeQL Analysis
      uses: github/codeql-action/analyze@v3
```

**Files to Modify**:
- `.github/workflows/ci.yml` (add security-scan job)

**Success Criteria**:
- CodeQL scan completes without critical issues
- Trivy scan passes or flags are addressed
- Security tab shows scan results

---

### Phase 5: Add Code Quality Checks (1 PR)
**Goal**: Enforce code formatting and static analysis

**Task 5.1 - Add Linting Job** (x86_64 only):
```yaml
lint:
  name: Code Quality
  runs-on: ubuntu-22.04
  steps:
    - uses: actions/checkout@v4

    - name: Install Tools
      run: |
        sudo apt-get install -y clang-format-18 clang-tidy-18

    - name: Check Formatting
      run: make format-check

    - name: Run Clang-Tidy
      run: |
        make configure PRESET=dev
        find src -name "*.cpp" | xargs clang-tidy-18 -p build/dev
```

**Files to Modify**:
- `.github/workflows/ci.yml` (add lint job)
- Ensure `.clang-format` and `.clang-tidy` exist

**Success Criteria**:
- Format check passes
- Clang-tidy analysis completes
- No critical warnings

---

### Phase 6: Configure Branch Protection (GitHub UI)
**Goal**: Enforce CI checks before merge

**Tasks**:
1. Navigate to: Settings → Branches → Branch protection rules
2. Add rule for `main` branch
3. Enable: "Require status checks to pass before merging"
4. Select required checks:
   - Validate Branch Name
   - Build & Test (x86_64)
   - Build & Test (ARM64)
   - Security Scan
   - Code Quality
5. Enable: "Require branches to be up to date before merging"

**Success Criteria**:
- Cannot merge PRs with failing checks
- All checks must be green before merge

---

## Cleanup Tasks

**After All Phases Complete**:
1. Close test PRs: #15, #18
2. Delete test workflow: `.github/workflows/test-pr.yml`
3. Delete backup: `.github/workflows/ci-old.yml.bak`
4. Delete analysis: `gemini_ci_issues_analysis.md` (DO NOT COMMIT)

---

## Important Notes

### What NOT to Do:
- ❌ Do not add all features at once
- ❌ Do not copy Gemini's full 152-line workflow directly
- ❌ Do not commit gemini_ci_issues_analysis.md

### Critical Success Factors:
1. **Test incrementally** - Each phase in separate PR
2. **Verify trigger works** - After each change, confirm `pull_request` events still fire
3. **Monitor job execution** - Check that jobs have non-zero steps (not silently rejected)

### If CI Stops Triggering:
1. Revert to last working version
2. Create minimal test workflow to isolate issue
3. Add features back one at a time

---

## Reference: Working Minimal CI (Baseline)

```yaml
name: CI

on:
  pull_request:

permissions:
  contents: read
  pull-requests: write

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - run: echo "CI works!"
```

**This is our safety net** - If anything breaks, revert to this structure and rebuild.

---

## Session Handoff Notes

**What Was Fixed This Session**:
- Identified ci.yml was silently rejected by GitHub Actions
- Created minimal test-pr.yml → proved pull_request events work
- Simplified ci.yml from 244 to 59 lines
- PR #19 successfully triggers CI workflows
- Branch validation works
- Build job runs (has dependency errors to fix)

**Next Session Should Start With**:
- Fix dependency/vcpkg issues in Build & Test job
- Get PR #19 fully green
- Merge PR #19 to main
- Then start Phase 2

**Key Files**:
- `.github/workflows/ci.yml` - Simplified working version
- `.github/workflows/test-pr.yml` - Minimal test (can delete after cleanup)
- `gemini_ci_issues_analysis.md` - Gemini's analysis (DO NOT COMMIT)
- `CI_IMPROVEMENT_PLAN.md` - This plan (COMMIT TO REPO)

---

Generated: 2025-11-29
Status: Ready for Phase 1 execution
