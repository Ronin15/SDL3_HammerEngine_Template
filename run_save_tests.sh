#!/bin/bash

# Helper script to build and run SaveGameManager tests

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
      echo -e "${BLUE}SaveGameManager Test Runner${NC}"
      echo -e "Usage: ./run_save_tests.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --clean      Clean test artifacts before building"
      echo -e "  --clean-all  Remove entire build directory and rebuild"
      echo -e "  --verbose    Run tests with verbose output"
      echo -e "  --help       Show this help message"
      exit 0
      ;;
  esac
done

# Handle clean-all case
if [ "$CLEAN_ALL" = true ]; then
  echo -e "${YELLOW}Removing entire build directory...${NC}"
  rm -rf build
fi

echo -e "${BLUE}Building SaveGameManager tests...${NC}"

# Ensure build directory exists
if [ ! -d "build" ]; then
  mkdir -p build
  echo -e "${YELLOW}Created build directory${NC}"
fi

# Navigate to build directory
cd build || { echo -e "${RED}Failed to enter build directory!${NC}"; exit 1; }

# Configure with CMake if needed
if [ ! -f "build.ninja" ]; then
  echo -e "${YELLOW}Configuring project with CMake and Ninja...${NC}"
  cmake -G Ninja .. || { echo -e "${RED}CMake configuration failed!${NC}"; exit 1; }
fi

# Clean tests if requested
if [ "$CLEAN" = true ]; then
  echo -e "${YELLOW}Cleaning test artifacts...${NC}"
  ninja -t clean save_manager_tests
fi

# Build the tests
echo -e "${YELLOW}Building tests...${NC}"
ninja save_manager_tests || { echo -e "${RED}Build failed!${NC}"; exit 1; }

# Check if test executable exists
TEST_EXECUTABLE="../bin/debug/save_manager_tests"
if [ ! -f "$TEST_EXECUTABLE" ]; then
  echo -e "${RED}Test executable not found at $TEST_EXECUTABLE${NC}"
  echo -e "${YELLOW}Searching for test executable...${NC}"
  FOUND_EXECUTABLE=$(find .. -name "save_manager_tests" -type f -executable | head -n 1)
  if [ -n "$FOUND_EXECUTABLE" ]; then
    TEST_EXECUTABLE="$FOUND_EXECUTABLE"
    echo -e "${GREEN}Found test executable at $TEST_EXECUTABLE${NC}"
  else
    echo -e "${RED}Could not find test executable!${NC}"
    exit 1
  fi
fi

# Run the tests
echo -e "${GREEN}Build successful. Running tests...${NC}"
echo -e "${BLUE}====================================${NC}"

# Run with appropriate options
if [ "$VERBOSE" = true ]; then
  "$TEST_EXECUTABLE" --log_level=all | tee test_output.log
else
  "$TEST_EXECUTABLE" | tee test_output.log
fi

TEST_RESULT=$?
echo -e "${BLUE}====================================${NC}"

# Create test_results directory if it doesn't exist
mkdir -p ../test_results

# Save test results
cp test_output.log "../test_results/save_manager_test_output.txt"

# Extract performance metrics if they exist
echo -e "${YELLOW}Saving test results...${NC}"
grep -E "time:|performance|saved:|loaded:" test_output.log > "../test_results/save_manager_performance_metrics.txt" || true

# Clean up temporary file
rm test_output.log

# Report test results
if [ $TEST_RESULT -eq 0 ]; then
  echo -e "${GREEN}All tests passed!${NC}"
else
  echo -e "${RED}Some tests failed. Please check the output above.${NC}"
fi

exit $TEST_RESULT