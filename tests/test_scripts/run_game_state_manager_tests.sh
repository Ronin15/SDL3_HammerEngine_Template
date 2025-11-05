#!/bin/bash

# Helper script to build and run GameStateManager tests

# Set up colored output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Process command line arguments
CLEAN=false
CLEAN_ALL=false
VERBOSE=false
TEST_FILTER=""

for arg in "$@"; do
  case $arg in
    --clean)
      CLEAN=true
      shift
      ;;
    --clean-all)
      CLEAN_ALL=true
      shift
      ;;
    --verbose)
      VERBOSE=true
      shift
      ;;
    --help)
      echo -e "${BLUE}GameStateManager Test Runner${NC}"
      echo -e "Usage: ./run_game_state_manager_tests.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --clean      Clean test artifacts before building"
      echo -e "  --clean-all  Remove entire build directory and rebuild"
      echo -e "  --verbose    Run tests with verbose output"
      echo -e "  --basic-test Run only basic state operations tests"
      echo -e "  --stack-test Run only state stack behavior tests"
      echo -e "  --lifecycle-test Run only state lifecycle tests"
      echo -e "  --deferred-test Run only deferred state change tests"
      echo -e "  --help       Show this help message"
      exit 0
      ;;
    --basic-test)
      TEST_FILTER="--run_test=*Add*,*Push*,*Pop*,*Change*"
      shift
      ;;
    --stack-test)
      TEST_FILTER="--run_test=*Stack*,*Pause*,*Resume*"
      shift
      ;;
    --lifecycle-test)
      TEST_FILTER="--run_test=*Update*,*Render*,*HandleInput*"
      shift
      ;;
    --deferred-test)
      TEST_FILTER="--run_test=*Request*"
      shift
      ;;
  esac
done

# Handle clean-all case
if [ "$CLEAN_ALL" = true ]; then
  echo -e "${YELLOW}Removing entire build directory...${NC}"
  rm -rf build
fi

echo -e "${BLUE}Running GameStateManager tests...${NC}"

# Get the directory where this script is located and find project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Check if test executable exists
TEST_EXECUTABLE="$PROJECT_ROOT/bin/debug/game_state_manager_tests"
if [ ! -f "$TEST_EXECUTABLE" ]; then
  echo -e "${RED}Test executable not found at $TEST_EXECUTABLE${NC}"
  echo -e "${YELLOW}Searching for test executable...${NC}"
  FOUND_EXECUTABLE=$(find "$PROJECT_ROOT" -name "game_state_manager_tests" -type f -executable | head -n 1)
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

# Change to project root directory before running tests
cd "$PROJECT_ROOT"

# Run with appropriate options
if [ "$VERBOSE" = true ]; then
  "$TEST_EXECUTABLE" $TEST_FILTER --log_level=all 2>&1 | tee "$PROJECT_ROOT/test_output.log"
else
  "$TEST_EXECUTABLE" $TEST_FILTER 2>&1 | tee "$PROJECT_ROOT/test_output.log"
fi

TEST_RESULT=${PIPESTATUS[0]}
echo -e "${BLUE}====================================${NC}"

# Create test_results directory if it doesn't exist
mkdir -p "$PROJECT_ROOT/test_results"

# Save test results with timestamp
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
cp "$PROJECT_ROOT/test_output.log" "$PROJECT_ROOT/test_results/game_state_manager_test_output_${TIMESTAMP}.txt"
# Also save to the standard location for compatibility
cp "$PROJECT_ROOT/test_output.log" "$PROJECT_ROOT/test_results/game_state_manager_test_output.txt"

# Extract test cases that were run
echo -e "\n=== Test Cases Executed ===" > "$PROJECT_ROOT/test_results/game_state_manager_test_cases.txt"
grep -E "Entering test case|Test case.*passed" "$PROJECT_ROOT/test_output.log" >> "$PROJECT_ROOT/test_results/game_state_manager_test_cases.txt" || true

# Extract just the test case names for easy reporting
grep -E "Entering test case" "$PROJECT_ROOT/test_output.log" | sed 's/.*Entering test case "\([^"]*\)".*/\1/' > "$PROJECT_ROOT/test_results/game_state_manager_test_cases_run.txt" || true

# Clean up temporary file
rm "$PROJECT_ROOT/test_output.log"

# Report test results
if [ $TEST_RESULT -eq 0 ]; then
  echo -e "${GREEN}All tests passed!${NC}"
  echo -e "${BLUE}Test results saved to:${NC} test_results/game_state_manager_test_output_${TIMESTAMP}.txt"
  
  # Print summary of test cases run
  echo -e "\n${BLUE}Test Cases Run:${NC}"
  if [ -f "$PROJECT_ROOT/test_results/game_state_manager_test_cases_run.txt" ] && [ -s "$PROJECT_ROOT/test_results/game_state_manager_test_cases_run.txt" ]; then
    while read -r testcase; do
      echo -e "  - ${testcase}"
    done < "$PROJECT_ROOT/test_results/game_state_manager_test_cases_run.txt"
  else
    echo -e "${YELLOW}  No test case details found.${NC}"
  fi
else
  echo -e "${RED}Some tests failed. Please check the output above.${NC}"
  echo -e "${YELLOW}Test results saved to:${NC} test_results/game_state_manager_test_output_${TIMESTAMP}.txt"
  
  # Print a summary of failed tests if available
  echo -e "\n${YELLOW}Failed Test Summary:${NC}"
  grep -E "FAILED|ASSERT" "$PROJECT_ROOT/test_results/game_state_manager_test_output.txt" || echo -e "${YELLOW}No specific failure details found.${NC}"
  
  # Print summary of test cases run
  echo -e "\n${BLUE}Test Cases Run:${NC}"
  if [ -f "$PROJECT_ROOT/test_results/game_state_manager_test_cases_run.txt" ] && [ -s "$PROJECT_ROOT/test_results/game_state_manager_test_cases_run.txt" ]; then
    while read -r testcase; do
      echo -e "  - ${testcase}"
    done < "$PROJECT_ROOT/test_results/game_state_manager_test_cases_run.txt"
  else
    echo -e "${YELLOW}  No test case details found.${NC}"
  fi
fi

exit $TEST_RESULT