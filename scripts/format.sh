#!/bin/bash
# Format all C++ source files using clang-format

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if clang-format is available
if ! command -v clang-format &> /dev/null; then
    echo -e "${RED}Error: clang-format not found${NC}"
    echo "Install: brew install clang-format (macOS) or apt install clang-format (Linux)"
    exit 1
fi

echo -e "${YELLOW}Formatting C++ files...${NC}"

# Find and format all C++ files
FORMATTED=0
find src tests -type f \( -name "*.cpp" -o -name "*.hpp" \) | while read file; do
    echo "Formatting: $file"
    clang-format -i "$file"
    ((FORMATTED++))
done

echo -e "${GREEN}✅ Formatted C++ files${NC}"
echo ""

# Format website if requested
if [ "$1" = "--all" ] || [ "$1" = "-a" ]; then
    if [ -d "website" ] && [ -f "website/package.json" ]; then
        echo -e "${YELLOW}Formatting website files...${NC}"
        cd website
        if command -v npm &> /dev/null; then
            npm run format 2>/dev/null || echo "No format script in package.json"
        fi
        cd ..
        echo -e "${GREEN}✅ Formatted website files${NC}"
    fi
fi

echo ""
echo -e "${GREEN}All done!${NC}"
