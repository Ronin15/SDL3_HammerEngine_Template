#!/bin/bash

# Helper script to run ThreadSystem tests

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
      echo -e "${BLUE}ThreadSystem Test Runner${NC}"
      echo -e "Usage: ./run_thread_tests.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --verbose    Run tests with verbose output"
      echo -e "  --help       Show this help message"
      exit 0
      ;;
  esac
done

echo -e "${BLUE}Running ThreadSystem tests...${NC}"

# Get the directory where this script is located and find project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Check if test executable exists
TEST_EXECUTABLE="$PROJECT_ROOT/bin/debug/thread_system_tests"
if [ ! -f "$TEST_EXECUTABLE" ]; then
  echo -e "${RED}Test executable not found at $TEST_EXECUTABLE${NC}"
  echo -e "${YELLOW}Searching for test executable...${NC}"
  FOUND_EXECUTABLE=$(find "$PROJECT_ROOT" -name "thread_system_tests" -type f -executable | head -n 1)
  if [ -n "$FOUND_EXECUTABLE" ]; then
    TEST_EXECUTABLE="$FOUND_EXECUTABLE"
    echo -e "${GREEN}Found test executable at $TEST_EXECUTABLE${NC}"
  else
    echo -e "${RED}Could not find test executable!${NC}"
    exit 1
  fi
fi

# Run the tests
echo -e "${GREEN}Running tests...${NC}"
echo -e "${BLUE}====================================${NC}"

# Run with appropriate options
if [ "$VERBOSE" = true ]; then
  "$TEST_EXECUTABLE" --log_level=all 2>&1 | tee test_output.log
else
  "$TEST_EXECUTABLE" 2>&1 | tee test_output.log
fi

TEST_RESULT=${PIPESTATUS[0]}
echo -e "${BLUE}====================================${NC}"

# Create test_results directory if it doesn't exist
mkdir -p "$PROJECT_ROOT/test_results"

# Check if there were any failures in the output
FAILURES=$(grep -o "[0-9]\+ failure" test_output.log 2>/dev/null | grep -o "[0-9]\+" || echo "0")

# Save test results
if [ -f test_output.log ]; then
  cp test_output.log "$PROJECT_ROOT/test_results/thread_system_test_output.txt"
fi

# Extract performance metrics if they exist
echo -e "${YELLOW}Saving test results...${NC}"
if [ -f test_output.log ]; then
  grep -E "time:|performance|tasks:|queue:" test_output.log > "$PROJECT_ROOT/test_results/thread_system_performance_metrics.txt" || true
  # Clean up temporary file
  rm test_output.log
fi

# Report test results
if [ $TEST_RESULT -eq 0 ] && [ "$FAILURES" = "0" ]; then
  echo -e "${GREEN}All tests passed!${NC}"
else
  if [ "$FAILURES" = "0" ]; then
    echo -e "${RED}Tests failed with exit code $TEST_RESULT. Please check the output above.${NC}"
  else
    echo -e "${RED}Tests failed! Found $FAILURES failure(s). Please check the output above.${NC}"
  fi
  TEST_RESULT=1
fi

exit $TEST_RESULT