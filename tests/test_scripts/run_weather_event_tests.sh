#!/bin/bash

# Weather Event Test Runner
# Copyright (c) 2025 Hammer Forged Games

# Set up colored output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Process command line arguments
CLEAN=false
CLEAN_ALL=false
VERBOSE=false
BUILD_TYPE="Debug"

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
    --debug)
      BUILD_TYPE="Debug"
      shift
      ;;
    --release)
      BUILD_TYPE="Release"
      shift
      ;;
    --help)
      echo -e "${BLUE}Weather Event Test Runner${NC}"
      echo -e "Usage: ./run_weather_event_tests.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --clean        Clean test artifacts before building"
      echo -e "  --clean-all    Remove entire build directory and rebuild"
      echo -e "  --verbose      Run tests with verbose output"
      echo -e "  --debug        Use debug build (default)"
      echo -e "  --release      Use release build"
      echo -e "  --help         Show this help message"
      echo -e "\nTest Suite:"
      echo -e "  Weather Event Tests:    Weather system event handling and integration"
      echo -e "\nExecution Time:"
      echo -e "  Weather tests:          ~30-45 seconds"
      echo -e "\nExamples:"
      echo -e "  ./run_weather_event_tests.sh              # Run weather event tests"
      echo -e "  ./run_weather_event_tests.sh --verbose    # Run with detailed output"
      exit 0
      ;;
  esac
done

echo -e "${BLUE}======================================================${NC}"
echo -e "${BLUE}           Weather Event Test Runner                ${NC}"
echo -e "${BLUE}======================================================${NC}"

# Get the directory where this script is located and find project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo -e "${YELLOW}Execution Plan: Weather Event tests${NC}"
echo -e "${CYAN}Testing weather system event handling and integration${NC}"
echo -e "${CYAN}Build type: ${BUILD_TYPE}${NC}"

# Track overall success
OVERALL_SUCCESS=true
PASSED_COUNT=0
FAILED_COUNT=0
TOTAL_COUNT=1

# Create test results directory
mkdir -p "$PROJECT_ROOT/test_results/weather_events"
COMBINED_RESULTS="$PROJECT_ROOT/test_results/weather_events/weather_event_tests_results.txt"
echo "Weather Event Tests Run $(date)" > "$COMBINED_RESULTS"

# Function to run weather event tests
run_weather_event_test() {
  local exec_name="weather_event_tests"
  
  echo -e "\n${MAGENTA}=====================================================${NC}"
  echo -e "${CYAN}Running Weather Event Tests${NC}"
  echo -e "${YELLOW}Test Suite: ${exec_name}${NC}"
  echo -e "${MAGENTA}=====================================================${NC}"
  
  # Determine the correct path to the test executable
  if [ "$BUILD_TYPE" = "Debug" ]; then
    TEST_EXECUTABLE="$PROJECT_ROOT/bin/debug/$exec_name"
  else
    TEST_EXECUTABLE="$PROJECT_ROOT/bin/release/$exec_name"
  fi
  
  # Check if test executable exists
  if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo -e "${RED}Test executable not found at $TEST_EXECUTABLE${NC}"
    echo -e "${YELLOW}Searching for test executable...${NC}"
    FOUND_EXECUTABLE=$(find "$PROJECT_ROOT" -name "$exec_name" -type f -executable | head -n 1)
    if [ -n "$FOUND_EXECUTABLE" ]; then
      TEST_EXECUTABLE="$FOUND_EXECUTABLE"
      echo -e "${GREEN}Found test executable at $TEST_EXECUTABLE${NC}"
    else
      echo -e "${RED}Could not find test executable!${NC}"
      echo "FAILED: $exec_name - executable not found" >> "$COMBINED_RESULTS"
      OVERALL_SUCCESS=false
      ((FAILED_COUNT++))
      return 1
    fi
  fi
  
  # Set test command options
  TEST_OPTS="--log_level=test_suite --catch_system_errors=no"
  if [ "$VERBOSE" = true ]; then
    TEST_OPTS="--log_level=all --report_level=detailed"
  fi
  
  # Create output file
  OUTPUT_FILE="$PROJECT_ROOT/test_results/weather_events/${exec_name}_output.txt"
  
  echo -e "${YELLOW}Running with options: $TEST_OPTS${NC}"
  
  # Check for timeout command availability
  TIMEOUT_CMD=""
  if command -v timeout &> /dev/null; then
    TIMEOUT_CMD="timeout"
  elif command -v gtimeout &> /dev/null; then
    TIMEOUT_CMD="gtimeout"
  fi
  
  # Set timeout duration
  local timeout_duration="60s"
  
  # Run the tests with timeout protection
  local test_result=0
  if [ -n "$TIMEOUT_CMD" ]; then
    if [ "$VERBOSE" = true ]; then
      $TIMEOUT_CMD $timeout_duration "$TEST_EXECUTABLE" $TEST_OPTS | tee "$OUTPUT_FILE"
      test_result=$?
    else
      $TIMEOUT_CMD $timeout_duration "$TEST_EXECUTABLE" $TEST_OPTS > "$OUTPUT_FILE" 2>&1
      test_result=$?
      # Show dots to indicate progress
      echo -e "${YELLOW}Test in progress..."
      tail -f "$OUTPUT_FILE" &
      TAIL_PID=$!
      sleep 2
      kill $TAIL_PID 2>/dev/null || true
    fi
  else
    if [ "$VERBOSE" = true ]; then
      "$TEST_EXECUTABLE" $TEST_OPTS | tee "$OUTPUT_FILE"
      test_result=$?
    else
      "$TEST_EXECUTABLE" $TEST_OPTS > "$OUTPUT_FILE" 2>&1
      test_result=$?
    fi
  fi
  
  # Display test output if not verbose
  if [ "$VERBOSE" = false ]; then
    cat "$OUTPUT_FILE"
  fi
  
  echo -e "${BLUE}====================================${NC}"
  
  # Save results with timestamp
  TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
  cp "$OUTPUT_FILE" "$PROJECT_ROOT/test_results/weather_events/${exec_name}_output_${TIMESTAMP}.txt"
  
  # Extract performance metrics and test summary
  grep -E "time:|performance|TestCase|Running.*test cases|failures detected|No errors detected" "$OUTPUT_FILE" > "$PROJECT_ROOT/test_results/weather_events/${exec_name}_summary.txt" || true
  
  # Handle timeout scenario
  if [ -n "$TIMEOUT_CMD" ] && [ $test_result -eq 124 ]; then
    echo -e "${RED}⚠️ Test execution timed out after $timeout_duration!${NC}"
    echo "FAILED: $exec_name - timed out after $timeout_duration" >> "$COMBINED_RESULTS"
    OVERALL_SUCCESS=false
    ((FAILED_COUNT++))
    return 1
  fi
  
  # Check test results
  if [ $test_result -eq 0 ] && ! grep -q "failure\|test cases failed\|errors detected.*[1-9]" "$OUTPUT_FILE"; then
    echo -e "\n${GREEN}✓ Weather Event tests completed successfully${NC}"
    
    # Extract test count information
    local test_count=$(grep -o "Running [0-9]\+ test cases" "$OUTPUT_FILE" | grep -o "[0-9]\+" | head -n 1)
    if [ -n "$test_count" ]; then
      echo -e "${GREEN}✓ All $test_count test cases passed${NC}"
    fi
    
    echo "PASSED: $exec_name" >> "$COMBINED_RESULTS"
    ((PASSED_COUNT++))
    return 0
  else
    echo -e "\n${RED}✗ Weather Event tests failed${NC}"
    
    # Show failure summary
    echo -e "\n${YELLOW}Failure Summary:${NC}"
    grep -E "failure|FAILED|error.*in.*:" "$OUTPUT_FILE" | head -n 5 || echo -e "${YELLOW}No specific failure details found.${NC}"
    
    echo "FAILED: $exec_name (exit code: $test_result)" >> "$COMBINED_RESULTS"
    OVERALL_SUCCESS=false
    ((FAILED_COUNT++))
    return 1
  fi
}

# Run the weather event tests
run_weather_event_test

# Print summary
echo -e "\n${BLUE}======================================================${NC}"
echo -e "${BLUE}            Weather Event Test Summary              ${NC}"
echo -e "${BLUE}======================================================${NC}"
echo -e "Total test suites: $TOTAL_COUNT"
echo -e "${GREEN}Passed: $PASSED_COUNT${NC}"
echo -e "${RED}Failed: $FAILED_COUNT${NC}"

# Save summary to results file
echo -e "\nSummary:" >> "$COMBINED_RESULTS"
echo "Total: $TOTAL_COUNT" >> "$COMBINED_RESULTS"
echo "Passed: $PASSED_COUNT" >> "$COMBINED_RESULTS"
echo "Failed: $FAILED_COUNT" >> "$COMBINED_RESULTS"
echo "Completed at: $(date)" >> "$COMBINED_RESULTS"

# Exit with appropriate status code and summary
if [ "$OVERALL_SUCCESS" = true ]; then
  echo -e "\n${GREEN}🎉 Weather Event tests completed successfully!${NC}"
  echo -e "${GREEN}✓ Weather system event handling: Verified${NC}"
  echo -e "${BLUE}Test results saved to: ${YELLOW}test_results/weather_events/${NC}"
  exit 0
else
  echo -e "\n${RED}❌ Weather Event tests failed!${NC}"
  echo -e "${YELLOW}Please check the individual test results in test_results/weather_events/${NC}"
  echo -e "${YELLOW}Combined results saved to: ${COMBINED_RESULTS}${NC}"
  exit 1
fi