#!/usr/bin/env bash
# Tool validation script for profiling and benchmarking
# Checks for required tools and provides installation instructions

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Track missing tools
MISSING_TOOLS=()
ALL_OK=true

# Function to check if a command exists
check_command() {
    local cmd=$1
    local name=$2
    local install_info=$3

    if command -v "$cmd" &>/dev/null; then
        echo -e "${GREEN}✓${NC} $name found: $(command -v $cmd)"
        return 0
    else
        echo -e "${RED}✗${NC} $name not found"
        MISSING_TOOLS+=("$name|$install_info")
        ALL_OK=false
        return 1
    fi
}

# Function to print installation instructions
print_install_instructions() {
    if [ ${#MISSING_TOOLS[@]} -eq 0 ]; then
        return
    fi

    echo ""
    echo -e "${YELLOW}==> Installation Instructions:${NC}"
    echo ""

    for tool in "${MISSING_TOOLS[@]}"; do
        IFS='|' read -r name install <<< "$tool"
        echo -e "${YELLOW}$name:${NC}"
        echo "  $install"
        echo ""
    done
}

# If specific tool is requested, check only that
if [ $# -eq 1 ]; then
    TOOL=$1
    case $TOOL in
        perf)
            check_command "perf" "perf" \
                "Linux: sudo apt-get install linux-tools-common linux-tools-generic\n  macOS: perf not available (use Instruments instead)"
            ;;
        pprof)
            check_command "pprof" "pprof (gperftools)" \
                "Linux: sudo apt-get install google-perftools\n  macOS: brew install gperftools"
            ;;
        wrk)
            check_command "wrk" "wrk" \
                "Linux: sudo apt-get install wrk\n  macOS: brew install wrk"
            ;;
        h2load)
            check_command "h2load" "h2load (nghttp2)" \
                "Linux: sudo apt-get install nghttp2-client\n  macOS: brew install nghttp2"
            ;;
        *)
            echo "Unknown tool: $TOOL"
            exit 1
            ;;
    esac

    print_install_instructions

    if [ "$ALL_OK" = true ]; then
        exit 0
    else
        exit 1
    fi
fi

# Check all tools
echo "==> Checking profiling and benchmarking tools..."
echo ""

# CPU Profiling Tools
echo "CPU Profiling Tools:"
check_command "pprof" "pprof (gperftools)" \
    "Linux: sudo apt-get install google-perftools\n  macOS: brew install gperftools"

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    check_command "perf" "perf" \
        "Linux: sudo apt-get install linux-tools-common linux-tools-generic"

    # Check for FlameGraph
    if [ -d "tools/flamegraph" ]; then
        echo -e "${GREEN}✓${NC} FlameGraph found: tools/flamegraph"
    else
        echo -e "${YELLOW}⚠${NC}  FlameGraph not found (optional)"
        MISSING_TOOLS+=("FlameGraph|git clone https://github.com/brendangregg/FlameGraph.git tools/flamegraph")
    fi
fi

echo ""

# Benchmarking Tools
echo "Benchmarking Tools:"
check_command "wrk" "wrk (HTTP/1.1 benchmarking)" \
    "Linux: sudo apt-get install wrk\n  macOS: brew install wrk\n  or build from source: git clone https://github.com/wg/wrk.git && cd wrk && make"

check_command "h2load" "h2load (HTTP/2 benchmarking)" \
    "Linux: sudo apt-get install nghttp2-client\n  macOS: brew install nghttp2"

echo ""

# Python and dependencies
echo "Python Tools:"
check_command "python3" "Python 3" \
    "Linux: sudo apt-get install python3 python3-pip\n  macOS: brew install python3"

if command -v python3 &>/dev/null; then
    # Check for required Python packages
    if python3 -c "import requests" 2>/dev/null; then
        echo -e "${GREEN}✓${NC} Python requests module found"
    else
        echo -e "${YELLOW}⚠${NC}  Python requests module not found"
        MISSING_TOOLS+=("Python requests|pip3 install requests")
    fi

    if python3 -c "import jinja2" 2>/dev/null; then
        echo -e "${GREEN}✓${NC} Python jinja2 module found"
    else
        echo -e "${YELLOW}⚠${NC}  Python jinja2 module not found"
        MISSING_TOOLS+=("Python jinja2|pip3 install jinja2")
    fi
fi

echo ""

# Summary
if [ "$ALL_OK" = true ]; then
    echo -e "${GREEN}✅ All required tools are installed!${NC}"
else
    echo -e "${RED}❌ Some tools are missing${NC}"
    print_install_instructions
    exit 1
fi
