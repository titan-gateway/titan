#!/bin/bash
# Run linting checks on C++ code

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}═══════════════════════════════════════${NC}"
echo -e "${BLUE}   Titan API Gateway - Lint Checks${NC}"
echo -e "${BLUE}═══════════════════════════════════════${NC}"
echo ""

ERRORS=0

# ============================================================================
# 1. Check clang-format
# ============================================================================
echo -e "${YELLOW}[1/3] Checking code formatting (clang-format)...${NC}"

if ! command -v clang-format &> /dev/null; then
    echo -e "${RED}  ✗ clang-format not found${NC}"
    echo "    Install: brew install clang-format (macOS) or apt install clang-format (Linux)"
    ((ERRORS++))
else
    # Check if files need formatting
    UNFORMATTED=$(find src tests -type f \( -name "*.cpp" -o -name "*.hpp" \) -exec clang-format --dry-run --Werror {} \; 2>&1 | wc -l)
    
    if [ "$UNFORMATTED" -gt 0 ]; then
        echo -e "${RED}  ✗ Some files need formatting${NC}"
        echo "    Run: ./scripts/format.sh"
        find src tests -type f \( -name "*.cpp" -o -name "*.hpp" \) -exec clang-format --dry-run --Werror {} \; 2>&1 | head -20
        ((ERRORS++))
    else
        echo -e "${GREEN}  ✓ All files are properly formatted${NC}"
    fi
fi

echo ""

# ============================================================================
# 2. Check clang-tidy (if available)
# ============================================================================
echo -e "${YELLOW}[2/3] Running static analysis (clang-tidy)...${NC}"

if ! command -v clang-tidy &> /dev/null; then
    echo -e "${YELLOW}  ⚠ clang-tidy not found (optional)${NC}"
    echo "    Install: brew install llvm (macOS) or apt install clang-tidy (Linux)"
else
    # Check if compile_commands.json exists
    if [ ! -f "build/dev/compile_commands.json" ]; then
        echo -e "${YELLOW}  ⚠ compile_commands.json not found${NC}"
        echo "    Run: cmake --preset=dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    else
        # Run clang-tidy on all source files
        TIDY_ERRORS=0
        find src -type f \( -name "*.cpp" \) | while read file; do
            clang-tidy -p build/dev "$file" --quiet 2>&1 | grep -i "error\|warning" && ((TIDY_ERRORS++)) || true
        done
        
        if [ "$TIDY_ERRORS" -gt 0 ]; then
            echo -e "${RED}  ✗ Found $TIDY_ERRORS issues${NC}"
            ((ERRORS++))
        else
            echo -e "${GREEN}  ✓ No issues found${NC}"
        fi
    fi
fi

echo ""

# ============================================================================
# 3. Check website (if requested)
# ============================================================================
if [ "$1" = "--all" ] || [ "$1" = "-a" ]; then
    echo -e "${YELLOW}[3/3] Checking website code...${NC}"
    
    if [ -d "website" ] && [ -f "website/package.json" ]; then
        cd website
        if command -v npm &> /dev/null; then
            npm run lint 2>&1 || ((ERRORS++))
        fi
        cd ..
    else
        echo -e "${YELLOW}  ⚠ Website not found or package.json missing${NC}"
    fi
    echo ""
else
    echo -e "${BLUE}[3/3] Skipping website checks (use --all to include)${NC}"
    echo ""
fi

# ============================================================================
# Summary
# ============================================================================
echo -e "${BLUE}═══════════════════════════════════════${NC}"
if [ "$ERRORS" -eq 0 ]; then
    echo -e "${GREEN}✅ All lint checks passed!${NC}"
    exit 0
else
    echo -e "${RED}❌ $ERRORS check(s) failed${NC}"
    echo ""
    echo "Quick fixes:"
    echo "  - Format code:  ./scripts/format.sh"
    echo "  - Build project: cmake --preset=dev && cmake --build --preset=dev"
    exit 1
fi
