#!/bin/bash

# Helper script to run Buffer Utilization tests

# Set up colored output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Process command line arguments
VERBOSE=false

for arg in "$@"; do
  case $arg in
    --verbose)
      VERBOSE=true
      shift
      ;;
    --help)
      echo -e "${BLUE}Buffer Utilization Test Runner${NC}"
      echo -e "Usage: ./run_buffer_utilization_tests.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --verbose    Run tests with verbose output"
      echo -e "  --help       Show this help message"
      echo -e "\nDescription:"
      echo -e "  Tests WorkerBudget buffer thread utilization system"
      echo -e "  Validates dynamic scaling based on workload thresholds"
      echo -e "  Verifies correct allocation on various hardware tiers"
      exit 0
      ;;
  esac
done

echo -e "${BLUE}Running Buffer Utilization tests...${NC}"

# Get the directory where this script is located and find project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Check if test executable exists
TEST_EXECUTABLE="$PROJECT_ROOT/bin/debug/buffer_utilization_tests"
if [ ! -f "$TEST_EXECUTABLE" ]; then
  echo -e "${RED}Test executable not found at $TEST_EXECUTABLE${NC}"
  echo -e "${YELLOW}Searching for test executable...${NC}"
  FOUND_EXECUTABLE=$(find "$PROJECT_ROOT" -name "buffer_utilization_tests" -type f -executable | head -n 1)
  if [ -n "$FOUND_EXECUTABLE" ]; then
    TEST_EXECUTABLE="$FOUND_EXECUTABLE"
    echo -e "${GREEN}Found test executable at $TEST_EXECUTABLE${NC}"
  else
    echo -e "${RED}Could not find test executable!${NC}"
    echo -e "${YELLOW}Make sure the project is built: ninja -C build${NC}"
    exit 1
  fi
fi

# Run the tests
echo -e "${GREEN}Running WorkerBudget buffer utilization tests...${NC}"
echo -e "${BLUE}================================================${NC}"

# Run with appropriate options
TEMP_LOG="$PROJECT_ROOT/test_output.log"
if [ "$VERBOSE" = true ]; then
  "$TEST_EXECUTABLE" --log_level=all 2>&1 | tee "$TEMP_LOG"
else
  "$TEST_EXECUTABLE" 2>&1 | tee "$TEMP_LOG"
fi

TEST_RESULT=${PIPESTATUS[0]}
echo -e "${BLUE}================================================${NC}"

# Create test_results directory if it doesn't exist
mkdir -p "$PROJECT_ROOT/test_results"

# Check if there were any failures in the output
FAILURES=$(grep -o "[0-9]\+ failures\? are detected" "$TEMP_LOG" 2>/dev/null | grep -o "[0-9]\+" || echo "0")

# Save test results
if [ -f "$TEMP_LOG" ]; then
  cp "$TEMP_LOG" "$PROJECT_ROOT/test_results/buffer_utilization_test_output.txt"
fi

# Extract WorkerBudget allocation metrics if they exist
echo -e "${YELLOW}Saving test results and allocation metrics...${NC}"
if [ -f "$TEMP_LOG" ]; then
  grep -E "workers|allocation|buffer|utilization|tier:" "$TEMP_LOG" > "$PROJECT_ROOT/test_results/buffer_utilization_metrics.txt" || true

  # Extract specific allocation patterns for analysis
  grep -E "Base allocations|optimal.*workers|burst.*workers" "$TEMP_LOG" > "$PROJECT_ROOT/test_results/buffer_allocation_patterns.txt" || true

  # Clean up temporary file
  rm "$TEMP_LOG"
fi

# Report test results
if [ $TEST_RESULT -eq 0 ] && [ "$FAILURES" = "0" ]; then
  echo -e "${GREEN}All buffer utilization tests passed!${NC}"
  echo -e "${CYAN}Buffer thread system working correctly across all hardware tiers${NC}"
else
  if [ "$FAILURES" = "0" ]; then
    echo -e "${RED}Tests failed with exit code $TEST_RESULT. Please check the output above.${NC}"
  else
    echo -e "${RED}Tests failed! Found $FAILURES failure(s). Please check the output above.${NC}"
  fi
  echo -e "${YELLOW}Check saved results in test_results/ directory for detailed analysis${NC}"
  TEST_RESULT=1
fi

exit $TEST_RESULT