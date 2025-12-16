#!/bin/bash
# Focused clang-tidy analysis for SDL3_HammerEngine
# Shows only important safety and bug issues

# Don't exit on error - clang-tidy returns non-zero when it finds issues
set +e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${BLUE}=== SDL3 HammerEngine - clang-tidy Analysis ===${NC}"
echo ""

# Check if clang-tidy is available
if ! command -v clang-tidy &> /dev/null; then
    echo -e "${RED}Error: clang-tidy not found${NC}"
    echo "Install via: brew install llvm"
    echo "Then add to PATH: export PATH=\"/opt/homebrew/opt/llvm/bin:\$PATH\""
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd -P)"

# Check compile_commands.json exists
if [ ! -f "$PROJECT_ROOT/compile_commands.json" ]; then
    echo -e "${RED}Error: compile_commands.json not found${NC}"
    echo "Run cmake first: cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug"
    exit 1
fi

# Extract .cpp files from compile_commands.json, filter to project src/ only
FILES=$(grep '"file":' "$PROJECT_ROOT/compile_commands.json" | \
        sed 's/.*"file": "//;s/".*//' | \
        grep '/src/' | \
        grep -v '/build/' | \
        grep -v '/tests/' | \
        grep -v '/_deps/' | \
        tr '\n' ' ')

FILE_COUNT=$(echo "$FILES" | wc -w | tr -d ' ')
echo -e "${YELLOW}Analyzing $FILE_COUNT source files...${NC}"
echo ""

# Run clang-tidy with parallel execution
TEMP_OUTPUT=$(mktemp)
echo -e "${CYAN}Running analysis in parallel...${NC}"

# Use run-clang-tidy for parallel execution if available, else fall back to single-threaded
if command -v run-clang-tidy &> /dev/null; then
    run-clang-tidy -p "$PROJECT_ROOT" \
        -clang-tidy-binary="$(which clang-tidy)" \
        -config-file="$SCRIPT_DIR/.clang-tidy" \
        -quiet \
        $FILES > "$TEMP_OUTPUT" 2>&1
else
    # Fall back to xargs for parallel execution
    echo "$FILES" | tr ' ' '\n' | xargs -P 4 -I {} \
        clang-tidy -p "$PROJECT_ROOT/compile_commands.json" \
        --config-file="$SCRIPT_DIR/.clang-tidy" \
        --quiet {} >> "$TEMP_OUTPUT" 2>&1
fi

# Filter to only project files (not system headers)
PROJECT_WARNINGS=$(grep -E "/src/.*: warning:" "$TEMP_OUTPUT" 2>/dev/null || true)

# Apply suppressions from file
SUPPRESSIONS_FILE="$SCRIPT_DIR/clang_tidy_suppressions.txt"
if [ -f "$SUPPRESSIONS_FILE" ] && [ -n "$PROJECT_WARNINGS" ]; then
    # Read suppressions (format: file_pattern:check_name:reason)
    while IFS=: read -r file_pattern check_name reason || [ -n "$file_pattern" ]; do
        # Skip comments and empty lines
        [[ "$file_pattern" =~ ^#.*$ || -z "$file_pattern" ]] && continue
        # Filter out matching warnings
        PROJECT_WARNINGS=$(echo "$PROJECT_WARNINGS" | grep -v "${file_pattern}.*${check_name}" || true)
    done < "$SUPPRESSIONS_FILE"
fi

# Count by category (handle empty strings properly)
if [ -z "$PROJECT_WARNINGS" ]; then
    SAFETY_COUNT=0
    NARROWING_COUNT=0
    CONST_COUNT=0
    UNUSED_COUNT=0
    STYLE_COUNT=0
    PERF_COUNT=0
else
    SAFETY_COUNT=$(echo "$PROJECT_WARNINGS" | grep -c 'bugprone-\|clang-analyzer-' || echo 0)
    NARROWING_COUNT=$(echo "$PROJECT_WARNINGS" | grep -c 'narrowing-conversion' || echo 0)
    CONST_COUNT=$(echo "$PROJECT_WARNINGS" | grep -c 'misc-const-correctness' || echo 0)
    UNUSED_COUNT=$(echo "$PROJECT_WARNINGS" | grep -c 'misc-unused-parameters' || echo 0)
    STYLE_COUNT=$(echo "$PROJECT_WARNINGS" | grep -c 'readability-' || echo 0)
    PERF_COUNT=$(echo "$PROJECT_WARNINGS" | grep -c 'performance-' || true)
    # Ensure counts are integers
    SAFETY_COUNT=${SAFETY_COUNT:-0}
    NARROWING_COUNT=${NARROWING_COUNT:-0}
    CONST_COUNT=${CONST_COUNT:-0}
    UNUSED_COUNT=${UNUSED_COUNT:-0}
    STYLE_COUNT=${STYLE_COUNT:-0}
    PERF_COUNT=${PERF_COUNT:-0}
fi

# Get critical issues (safety-related)
CRITICAL=$(echo "$PROJECT_WARNINGS" | grep -E 'bugprone-|clang-analyzer-' | grep -v 'narrowing-conversion' 2>/dev/null || true)
NARROWING=$(echo "$PROJECT_WARNINGS" | grep 'narrowing-conversion' 2>/dev/null || true)

echo -e "${BLUE}=== Summary ===${NC}"
echo ""
echo -e "${RED}Safety Issues:${NC}"
echo "  Bugprone/Analyzer: $SAFETY_COUNT"
echo "  Narrowing conversions: $NARROWING_COUNT"
echo ""
echo -e "${YELLOW}Code Quality:${NC}"
echo "  Const correctness: $CONST_COUNT"
echo "  Unused parameters: $UNUSED_COUNT"
echo "  Style issues: $STYLE_COUNT"
echo "  Performance: $PERF_COUNT"
echo ""

# Show critical safety issues (excluding narrowing which is usually intentional in game code)
if [ -n "$CRITICAL" ]; then
    echo -e "${RED}=== Critical Safety Issues ===${NC}"
    echo "$CRITICAL" | head -50
    echo ""
fi

# Show narrowing conversions (common but worth reviewing)
if [ "$NARROWING_COUNT" -gt 0 ]; then
    echo -e "${YELLOW}=== Narrowing Conversions (top 20) ===${NC}"
    echo "$NARROWING" | head -20
    echo ""
fi

# Cleanup
rm -f "$TEMP_OUTPUT"

# Final status
TOTAL_SAFETY=$((SAFETY_COUNT))
if [ "$TOTAL_SAFETY" -eq 0 ]; then
    echo -e "${GREEN}✓ No critical safety issues found!${NC}"
    exit 0
else
    echo -e "${YELLOW}⚠ Found $TOTAL_SAFETY safety-related warnings to review${NC}"
    exit 0
fi
