#!/bin/bash

# Script to run collision system tests
# This script runs comprehensive tests for the spatial hash collision system

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
      echo -e "${BLUE}Collision System Tests Runner${NC}"
      echo -e "Usage: ./run_collision_tests.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --verbose    Run tests with verbose output"
      echo -e "  --help       Show this help message"
      echo -e "\nTests Coverage:"
      echo -e "  - AABB intersection and containment tests"
      echo -e "  - SpatialHash insertion, removal, and querying"
      echo -e "  - Collision system performance benchmarks"
      echo -e "  - Stress tests with high entity density"
      echo -e "  - Boundary condition edge cases"
      exit 0
      ;;
  esac
done

# Create results directory
mkdir -p "$PROJECT_DIR/test_results"
RESULTS_FILE="$PROJECT_DIR/test_results/collision_tests_results.txt"

echo -e "${BLUE}======================================================${NC}"
echo -e "${BLUE}            Collision System Tests                    ${NC}"
echo -e "${BLUE}======================================================${NC}"

# Check if the test executable exists
TEST_EXECUTABLE="$PROJECT_DIR/bin/debug/collision_system_tests"

if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo -e "${RED}Test executable not found: $TEST_EXECUTABLE${NC}"
    echo -e "${YELLOW}Make sure you have built the project with tests enabled.${NC}"
    echo -e "Run: ${CYAN}cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build${NC}"
    exit 1
fi

# Make sure the executable has proper permissions
chmod +x "$TEST_EXECUTABLE"

# Run the tests
echo -e "${CYAN}Running collision system tests...${NC}"
echo "Collision System Tests - $(date)" > "$RESULTS_FILE"

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
    echo -e "${GREEN}✓ All collision system tests passed successfully!${NC}"
    echo -e "\nTest Coverage:"
    echo -e "  ${GREEN}✓${NC} AABB geometric operations"
    echo -e "  ${GREEN}✓${NC} SpatialHash data structure operations"
    echo -e "  ${GREEN}✓${NC} Performance benchmarks"
    echo -e "  ${GREEN}✓${NC} Stress testing with high density"
    echo -e "  ${GREEN}✓${NC} Boundary condition handling"
else
    echo -e "${RED}✗ Some collision system tests failed${NC}"
    echo -e "${YELLOW}Check the detailed results in: $RESULTS_FILE${NC}"
fi

echo -e "${CYAN}Test results saved to: $RESULTS_FILE${NC}"
echo -e "${BLUE}======================================================${NC}"

exit $RESULT