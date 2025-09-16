#!/bin/bash

# Script to run pathfinding system tests
# This script runs comprehensive tests for the A* pathfinding system

# Set up colored output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/../.."

# Process command line arguments
VERBOSE=false

for arg in "$@"; do
  case $arg in
    --verbose)
      VERBOSE=true
      shift
      ;;
    --help)
      echo -e "${BLUE}Pathfinding System Tests Runner${NC}"
      echo -e "Usage: ./run_pathfinding_tests.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --verbose    Run tests with verbose output"
      echo -e "  --help       Show this help message"
      echo -e "\nTests Coverage:"
      echo -e "  - Grid coordinate conversion and bounds checking"
      echo -e "  - A* pathfinding algorithm correctness"
      echo -e "  - Diagonal movement toggle functionality"
      echo -e "  - Dynamic weight system for avoidance"
      echo -e "  - Performance benchmarks for various grid sizes"
      echo -e "  - Edge cases and error handling"
      exit 0
      ;;
  esac
done

# Create results directory
mkdir -p "$PROJECT_DIR/test_results"
RESULTS_FILE="$PROJECT_DIR/test_results/pathfinding_tests_results.txt"

echo -e "${BLUE}======================================================${NC}"
echo -e "${BLUE}           Pathfinding System Tests                  ${NC}"
echo -e "${BLUE}======================================================${NC}"

# Check if the test executable exists
TEST_EXECUTABLE="$PROJECT_DIR/bin/debug/pathfinding_system_tests"

if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo -e "${RED}Test executable not found: $TEST_EXECUTABLE${NC}"
    echo -e "${YELLOW}Make sure you have built the project with tests enabled.${NC}"
    echo -e "Run: ${CYAN}cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build${NC}"
    exit 1
fi

# Make sure the executable has proper permissions
chmod +x "$TEST_EXECUTABLE"

# Run the tests
echo -e "${CYAN}Running pathfinding system tests...${NC}"
echo "Pathfinding System Tests - $(date)" > "$RESULTS_FILE"

if [ "$VERBOSE" = true ]; then
    echo -e "${YELLOW}Verbose mode enabled${NC}"
    $TEST_EXECUTABLE --log_level=all 2>&1 | tee -a "$RESULTS_FILE"
    RESULT=${PIPESTATUS[0]}
else
    $TEST_EXECUTABLE --log_level=test_suite 2>&1 | tee -a "$RESULTS_FILE"
    RESULT=${PIPESTATUS[0]}
fi

echo "" >> "$RESULTS_FILE"
echo "Test completed at: $(date)" >> "$RESULTS_FILE"
echo "Exit code: $RESULT" >> "$RESULTS_FILE"

# Report results
echo -e "\n${BLUE}======================================================${NC}"
if [ $RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ All pathfinding system tests passed successfully!${NC}"
    echo -e "\nTest Coverage:"
    echo -e "  ${GREEN}✓${NC} Grid coordinate system operations"
    echo -e "  ${GREEN}✓${NC} A* pathfinding algorithm"
    echo -e "  ${GREEN}✓${NC} Movement configuration (diagonal/orthogonal)"
    echo -e "  ${GREEN}✓${NC} Dynamic weight system"
    echo -e "  ${GREEN}✓${NC} Performance benchmarks"
    echo -e "  ${GREEN}✓${NC} Error handling and edge cases"
else
    echo -e "${RED}✗ Some pathfinding system tests failed${NC}"
    echo -e "${YELLOW}Check the detailed results in: $RESULTS_FILE${NC}"
fi

echo -e "${CYAN}Test results saved to: $RESULTS_FILE${NC}"
echo -e "${BLUE}======================================================${NC}"

exit $RESULT