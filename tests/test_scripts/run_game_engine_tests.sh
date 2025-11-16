#!/bin/bash
# Script to run the Game Engine Tests
# Copyright (c) 2025 Hammer Forged Games, MIT License

# Set colors for better readability
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${YELLOW}Running Game Engine Tests...${NC}"

# Set default build type
BUILD_TYPE="Debug"
VERBOSE=false

# Process command-line options
while [[ $# -gt 0 ]]; do
  case $1 in
    --debug)
      BUILD_TYPE="Debug"
      shift
      ;;
    --release)
      BUILD_TYPE="Release"
      shift
      ;;
    --verbose)
      VERBOSE=true
      shift
      ;;
    --help)
      echo "Usage: $0 [--debug] [--release] [--verbose] [--help]"
      echo "  --debug     Run in Debug mode (default)"
      echo "  --release   Run in Release mode"
      echo "  --verbose   Show verbose output"
      echo "  --help      Show this help message"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: $0 [--debug] [--release] [--verbose] [--help]"
      exit 1
      ;;
  esac
done

# Get the directory where this script is located and find project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Determine the correct path to the test executable
if [ "$BUILD_TYPE" = "Debug" ]; then
  TEST_EXECUTABLE="$PROJECT_ROOT/bin/debug/game_engine_tests"
else
  TEST_EXECUTABLE="$PROJECT_ROOT/bin/release/game_engine_tests"
fi

# Verify executable exists
if [ ! -f "$TEST_EXECUTABLE" ]; then
  echo -e "${RED}Error: Test executable not found at '$TEST_EXECUTABLE'${NC}"
  echo -e "${YELLOW}Searching for test executable...${NC}"
  FOUND_EXECUTABLE=$(find "$PROJECT_ROOT/bin" -name "game_engine_tests" -type f -executable | head -n 1)
  if [ -n "$FOUND_EXECUTABLE" ]; then
    echo -e "${GREEN}Found executable at: $FOUND_EXECUTABLE${NC}"
    TEST_EXECUTABLE="$FOUND_EXECUTABLE"
  else
    echo -e "${RED}Could not find the test executable. Build may have failed.${NC}"
    exit 1
  fi
fi

# Ensure test_results directory exists
mkdir -p "$PROJECT_ROOT/test_results"

# Output file
OUTPUT_FILE="$PROJECT_ROOT/test_results/game_engine_tests_output.txt"

# Set test command options
TEST_OPTS="--log_level=all --catch_system_errors=no"
if [ "$VERBOSE" = true ]; then
  TEST_OPTS="$TEST_OPTS --report_level=detailed"
fi

# Run the tests
echo -e "${YELLOW}Running Game Engine tests...${NC}"
"$TEST_EXECUTABLE" $TEST_OPTS 2>&1 | tee "$OUTPUT_FILE"
TEST_RESULT=${PIPESTATUS[0]}

# Check test status
if [ $TEST_RESULT -ne 0 ] || grep -q "failure\|test cases failed\|fatal error" "$OUTPUT_FILE"; then
  echo -e "${RED}❌ Some tests failed! See $OUTPUT_FILE for details.${NC}"
  exit 1
else
  echo -e "${GREEN}✅ All Game Engine tests passed!${NC}"
  exit 0
fi
