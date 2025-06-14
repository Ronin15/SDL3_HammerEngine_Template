#!/bin/bash

# Wrapper script for running focused cppcheck analysis from test_scripts directory
# This script calls the actual cppcheck_focused.sh from tests/cppcheck/

# Set up colored output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Path to the actual cppcheck script
CPPCHECK_DIR="$SCRIPT_DIR/../cppcheck"
CPPCHECK_SCRIPT="$CPPCHECK_DIR/cppcheck_focused.sh"

echo -e "${BLUE}=== Running Focused Cppcheck Static Analysis ===${NC}"
echo ""

# Check if cppcheck script exists
if [ ! -f "$CPPCHECK_SCRIPT" ]; then
    echo -e "${RED}Error: Cppcheck script not found at $CPPCHECK_SCRIPT${NC}"
    exit 1
fi

# Check if cppcheck is installed
if ! command -v cppcheck &> /dev/null; then
    echo -e "${RED}Error: cppcheck not found. Please install cppcheck first.${NC}"
    echo "Ubuntu/Debian: sudo apt-get install cppcheck"
    echo "macOS: brew install cppcheck"
    echo "Or download from: https://cppcheck.sourceforge.io/"
    exit 1
fi

# Make sure the script is executable
chmod +x "$CPPCHECK_SCRIPT"

# Change to cppcheck directory and run the analysis
cd "$CPPCHECK_DIR"

# Run the focused cppcheck analysis
./cppcheck_focused.sh
RESULT=$?

echo ""
if [ $RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ Cppcheck static analysis completed successfully${NC}"
    echo -e "${BLUE}Note: Focus on [error] and [warning] severity issues first${NC}"
else
    echo -e "${RED}✗ Cppcheck static analysis completed with issues detected${NC}"
    echo -e "${YELLOW}Review the output above for critical issues that need fixing${NC}"
fi

echo ""
echo -e "${CYAN}For detailed reports and documentation:${NC}"
echo -e "  Full analysis: cd tests/cppcheck && ./run_cppcheck.sh"
echo -e "  Documentation: tests/cppcheck/README.md"
echo -e "  Fix guide: tests/cppcheck/FIXES.md"

exit $RESULT
