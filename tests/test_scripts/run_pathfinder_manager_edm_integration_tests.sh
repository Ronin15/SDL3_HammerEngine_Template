#!/bin/bash
# PathfinderManager EDM Integration Test Runner
# Copyright 2025 Hammer Forged Games

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
      echo -e "${BLUE}PathfinderManager EDM Integration Test Runner${NC}"
      echo -e "Usage: ./run_pathfinder_manager_edm_integration_tests.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --verbose      Run tests with verbose output"
      echo -e "  --help         Show this help message"
      exit 0
      ;;
  esac
done

echo -e "${BLUE}Running PathfinderManager EDM Integration Tests${NC}"

# Get the directory where this script is located and find project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Track overall result
OVERALL_RESULT=0

# Check if test executable exists
TEST_EXECUTABLE="$PROJECT_ROOT/bin/debug/pathfinder_manager_edm_integration_tests"
if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo -e "${RED}Error: Test executable not found: pathfinder_manager_edm_integration_tests${NC}"
    echo -e "${YELLOW}Please build the project first with: cmake -B build -G Ninja && ninja -C build${NC}"
    exit 1
fi

# Set test options
TEST_OPTS="--log_level=test_suite --catch_system_errors=no"
if [ "$VERBOSE" = true ]; then
    TEST_OPTS="--log_level=all --report_level=detailed"
fi

# Create test results directory
mkdir -p "$PROJECT_ROOT/test_results"

# Run the test
echo -e "${BLUE}Running pathfinder_manager_edm_integration_tests...${NC}"
if [ "$VERBOSE" = true ]; then
    "$TEST_EXECUTABLE" $TEST_OPTS
else
    "$TEST_EXECUTABLE" $TEST_OPTS > "$PROJECT_ROOT/test_results/pathfinder_manager_edm_integration_tests_output.txt" 2>&1
fi

TEST_RESULT=$?

if [ $TEST_RESULT -ne 0 ]; then
    echo -e "${RED}pathfinder_manager_edm_integration_tests failed with exit code $TEST_RESULT${NC}"
    if [ "$VERBOSE" = false ]; then
        echo -e "${YELLOW}Output:${NC}"
        tail -20 "$PROJECT_ROOT/test_results/pathfinder_manager_edm_integration_tests_output.txt"
    fi
    OVERALL_RESULT=1
else
    echo -e "${GREEN}pathfinder_manager_edm_integration_tests completed successfully${NC}"
fi

# Show summary
if [ $OVERALL_RESULT -ne 0 ]; then
    echo -e "\n${RED}PathfinderManager EDM Integration tests failed!${NC}"
    exit 1
else
    echo -e "\n${GREEN}All PathfinderManager EDM Integration tests completed successfully!${NC}"
    exit 0
fi
