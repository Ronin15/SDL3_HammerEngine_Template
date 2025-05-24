#!/bin/bash

# Script to run all test shell scripts sequentially

# Set up colored output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Directory where all scripts are located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Process command line arguments
VERBOSE=false

for arg in "$@"; do
  case $arg in
    --verbose)
      VERBOSE=true
      shift
      ;;
    --help)
      echo -e "${BLUE}All Tests Runner${NC}"
      echo -e "Usage: ./run_all_tests.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --verbose    Run tests with verbose output"
      echo -e "  --help       Show this help message"
      exit 0
      ;;
  esac
done

# Find all test shell scripts in the directory
TEST_SCRIPTS=(
  "$SCRIPT_DIR/run_thread_tests.sh"
  "$SCRIPT_DIR/run_thread_safe_ai_tests.sh"
  "$SCRIPT_DIR/run_thread_safe_ai_integration_tests.sh"
  "$SCRIPT_DIR/run_ai_benchmark.sh"
  "$SCRIPT_DIR/run_ai_optimization_tests.sh"
  "$SCRIPT_DIR/run_save_tests.sh"
)

# Create a directory for the combined test results
mkdir -p "$SCRIPT_DIR/test_results/combined"
COMBINED_RESULTS="$SCRIPT_DIR/test_results/combined/all_tests_results.txt"
echo "All Tests Run $(date)" > "$COMBINED_RESULTS"

# Track overall success
OVERALL_SUCCESS=true
PASSED_COUNT=0
FAILED_COUNT=0
TOTAL_COUNT=${#TEST_SCRIPTS[@]}

# Function to run a test script
run_test_script() {
  local script=$1
  local script_name=$(basename "$script")
  local args=""
  
  # Pass along relevant flags
  if [ "$VERBOSE" = true ]; then
    args="$args --verbose"
  fi
  
  echo -e "\n${MAGENTA}=====================================================${NC}"
  echo -e "${CYAN}Running test script: ${YELLOW}$script_name${NC}"
  echo -e "${MAGENTA}=====================================================${NC}"
  
  # Check if the script exists and is executable
  if [ ! -f "$script" ]; then
    echo -e "${RED}Script not found: $script${NC}"
    echo "FAILED: Script not found: $script_name" >> "$COMBINED_RESULTS"
    OVERALL_SUCCESS=false
    ((FAILED_COUNT++))
    return 1
  fi
  
  # Make sure the script is executable
  chmod +x "$script"
  
  # Run the script with provided arguments
  $script $args
  local result=$?
  
  if [ $result -eq 0 ]; then
    echo -e "\n${GREEN}✓ Test script $script_name completed successfully${NC}"
    echo "PASSED: $script_name" >> "$COMBINED_RESULTS"
    ((PASSED_COUNT++))
    return 0
  else
    echo -e "\n${RED}✗ Test script $script_name failed with exit code $result${NC}"
    echo "FAILED: $script_name (exit code: $result)" >> "$COMBINED_RESULTS"
    OVERALL_SUCCESS=false
    ((FAILED_COUNT++))
    return 1
  fi
}

# Print header
echo -e "${BLUE}======================================================${NC}"
echo -e "${BLUE}              Running All Test Scripts                ${NC}"
echo -e "${BLUE}======================================================${NC}"
echo -e "${YELLOW}Found ${#TEST_SCRIPTS[@]} test scripts to run${NC}"

# Run each test script
for script in "${TEST_SCRIPTS[@]}"; do
  run_test_script "$script"
  
  # Add a small delay between tests to ensure resources are released
  sleep 2
done

# Print summary
echo -e "\n${BLUE}======================================================${NC}"
echo -e "${BLUE}                  Test Summary                       ${NC}"
echo -e "${BLUE}======================================================${NC}"
echo -e "Total scripts: $TOTAL_COUNT"
echo -e "${GREEN}Passed: $PASSED_COUNT${NC}"
echo -e "${RED}Failed: $FAILED_COUNT${NC}"

# Save summary to results file
echo -e "\nSummary:" >> "$COMBINED_RESULTS"
echo "Total: $TOTAL_COUNT" >> "$COMBINED_RESULTS"
echo "Passed: $PASSED_COUNT" >> "$COMBINED_RESULTS"
echo "Failed: $FAILED_COUNT" >> "$COMBINED_RESULTS"
echo "Completed at: $(date)" >> "$COMBINED_RESULTS"

# Exit with appropriate status code
if [ "$OVERALL_SUCCESS" = true ]; then
  echo -e "\n${GREEN}All test scripts completed successfully!${NC}"
  exit 0
else
  echo -e "\n${RED}Some test scripts failed. Please check the individual test results.${NC}"
  echo -e "Combined results saved to: ${YELLOW}$COMBINED_RESULTS${NC}"
  exit 1
fi