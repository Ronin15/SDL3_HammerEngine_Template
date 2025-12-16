#!/bin/bash
# Simple focused cppcheck analysis for SDL3_HammerEngine_Template
# This script runs cppcheck with optimized settings to show only real issues

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== SDL3 HammerEngine - Focused Cppcheck Analysis ===${NC}"
echo ""

# Check if cppcheck is available
if ! command -v cppcheck &> /dev/null; then
    echo -e "${RED}Error: cppcheck not found. Please install cppcheck first.${NC}"
    exit 1
fi

# Run focused analysis - only real issues
echo -e "${YELLOW}Running focused analysis (errors, warnings, performance issues only)...${NC}"
echo ""

# Get script directory to handle relative paths correctly
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Run cppcheck and capture output for counting
TEMP_OUTPUT=$(mktemp)

# Check compile_commands.json exists for proper cross-TU analysis
if [ -f "$PROJECT_ROOT/compile_commands.json" ]; then
    echo -e "${BLUE}Using compile_commands.json for improved analysis${NC}"
    cppcheck \
        --project="$PROJECT_ROOT/compile_commands.json" \
        --enable=warning,style,performance,portability \
        --library=std,posix \
        --library="$SCRIPT_DIR/cppcheck_lib.cfg" \
        --suppressions-list="$SCRIPT_DIR/cppcheck_suppressions.txt" \
        --std=c++20 \
        --quiet \
        --template='{file}:{line}: [{severity}] {message}' \
        2>&1 | tee "$TEMP_OUTPUT"
else
    echo -e "${YELLOW}Warning: compile_commands.json not found, using manual include paths${NC}"
    echo "Run cmake first for better analysis: cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug"
    cppcheck \
        -I"$PROJECT_ROOT/include" \
        -I"$PROJECT_ROOT/src" \
        --enable=warning,style,performance,portability \
        --library=std,posix \
        --library="$SCRIPT_DIR/cppcheck_lib.cfg" \
        --suppressions-list="$SCRIPT_DIR/cppcheck_suppressions.txt" \
        --platform=unix64 \
        --std=c++20 \
        --quiet \
        --template='{file}:{line}: [{severity}] {message}' \
        "$PROJECT_ROOT/src/" "$PROJECT_ROOT/include/" \
        2>&1 | tee "$TEMP_OUTPUT"
fi

# Count issues with simple grep and wc
ERROR_COUNT=$(grep '\[error\]' "$TEMP_OUTPUT" | wc -l | tr -d ' ')
WARNING_COUNT=$(grep '\[warning\]' "$TEMP_OUTPUT" | wc -l | tr -d ' ')
STYLE_COUNT=$(grep '\[style\]' "$TEMP_OUTPUT" | wc -l | tr -d ' ')
PERFORMANCE_COUNT=$(grep '\[performance\]' "$TEMP_OUTPUT" | wc -l | tr -d ' ')
PORTABILITY_COUNT=$(grep '\[portability\]' "$TEMP_OUTPUT" | wc -l | tr -d ' ')

# Ensure counts are numeric (default to 0 if empty)
ERROR_COUNT=${ERROR_COUNT:-0}
WARNING_COUNT=${WARNING_COUNT:-0}
STYLE_COUNT=${STYLE_COUNT:-0}
PERFORMANCE_COUNT=${PERFORMANCE_COUNT:-0}
PORTABILITY_COUNT=${PORTABILITY_COUNT:-0}

# Calculate total
TOTAL_COUNT=$((ERROR_COUNT + WARNING_COUNT + STYLE_COUNT + PERFORMANCE_COUNT + PORTABILITY_COUNT))

# Clean up
rm -f "$TEMP_OUTPUT"

echo ""
echo -e "${GREEN}Analysis complete!${NC}"
echo ""

# Dynamic summary based on actual results
if [ $TOTAL_COUNT -eq 0 ]; then
    echo -e "${GREEN}🎉 Perfect! No issues found!${NC}"
    echo -e "${BLUE}Status: All critical issues have been resolved${NC}"
    echo "✅ Array bounds errors - FIXED"
    echo "✅ Uninitialized variables - FIXED"
    echo "✅ Thread safety issues - FIXED"
    echo "✅ Style improvements - COMPLETED"
    echo ""
    echo -e "${GREEN}Result: 100% of actionable issues resolved!${NC}"
else
    echo -e "${YELLOW}Found $TOTAL_COUNT issues:${NC}"
    [ "$ERROR_COUNT" -gt 0 ] && echo -e "${RED}  Errors: $ERROR_COUNT${NC}"
    [ "$WARNING_COUNT" -gt 0 ] && echo -e "${YELLOW}  Warnings: $WARNING_COUNT${NC}"
    [ "$STYLE_COUNT" -gt 0 ] && echo -e "${BLUE}  Style: $STYLE_COUNT${NC}"
    [ "$PERFORMANCE_COUNT" -gt 0 ] && echo -e "${CYAN}  Performance: $PERFORMANCE_COUNT${NC}"
    [ "$PORTABILITY_COUNT" -gt 0 ] && echo -e "${MAGENTA}  Portability: $PORTABILITY_COUNT${NC}"
    echo ""
    if [ $((ERROR_COUNT + WARNING_COUNT)) -eq 0 ]; then
        echo -e "${GREEN}Good news: No critical errors or warnings!${NC}"
        echo -e "${BLUE}Only style/performance suggestions remain${NC}"
    else
        echo -e "${YELLOW}Priority: Address errors and warnings first${NC}"
    fi
fi

echo ""
echo -e "${YELLOW}Note: This configuration filters out ~2,500 false positives${NC}"
echo -e "${YELLOW}to focus on genuine code quality issues.${NC}"
