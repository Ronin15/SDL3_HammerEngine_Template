#!/bin/bash

# Script to run PathfinderManager and AIManager contention tests
# This script runs integration tests for WorkerBudget coordination between systems

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
      echo -e "${BLUE}PathfinderManager & AIManager Contention Tests Runner${NC}"
      echo -e "Usage: ./run_pathfinder_ai_contention_tests.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --verbose    Enable verbose output"
      echo -e "  --help       Show this help message"
      echo -e "\nThis script tests WorkerBudget coordination between PathfinderManager"
      echo -e "and AIManager under heavy load, including:"
      echo -e "  • WorkerBudget allocation verification (19% pathfinding, 44% AI)"
      echo -e "  • Simultaneous AI and pathfinding load handling"
      echo -e "  • Worker starvation prevention"
      echo -e "  • Queue pressure coordination"
      echo -e "  • Rate limiting (50 requests/frame)"
      echo -e "  • Graceful degradation under stress"
      exit 0
      ;;
    *)
      echo -e "${RED}Unknown argument: $arg${NC}"
      echo -e "Use --help for usage information"
      exit 1
      ;;
  esac
done

# Create results directory
mkdir -p "$PROJECT_DIR/test_results"
RESULTS_FILE="$PROJECT_DIR/test_results/pathfinder_ai_contention_tests_results.txt"

echo -e "${BLUE}======================================================${NC}"
echo -e "${BLUE}   PathfinderManager & AIManager Contention Tests    ${NC}"
echo -e "${BLUE}======================================================${NC}"

# Check if the test executable exists
TEST_EXECUTABLE="$PROJECT_DIR/bin/debug/pathfinder_ai_contention_tests"

if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo -e "${RED}Test executable not found: $TEST_EXECUTABLE${NC}"
    echo -e "${YELLOW}Make sure you have built the project with tests enabled.${NC}"
    echo -e "Run: ${CYAN}cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build${NC}"
    exit 1
fi

# Make sure the executable has proper permissions
chmod +x "$TEST_EXECUTABLE"

# Run the tests
echo -e "${CYAN}Running PathfinderManager & AIManager contention tests...${NC}"
echo "PathfinderManager & AIManager Contention Tests - $(date)" > "$RESULTS_FILE"

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
    echo -e "${GREEN}✓ All PathfinderManager & AIManager contention tests passed!${NC}"
    echo -e "\nWorkerBudget Integration Verified:"
    echo -e "  ${GREEN}✓${NC} Pathfinding worker allocation (19%)"
    echo -e "  ${GREEN}✓${NC} AI worker allocation (44%)"
    echo -e "  ${GREEN}✓${NC} Simultaneous load handling (100-200 requests)"
    echo -e "  ${GREEN}✓${NC} Worker starvation prevention"
    echo -e "  ${GREEN}✓${NC} Queue pressure coordination"
    echo -e "  ${GREEN}✓${NC} Rate limiting (50 requests/frame)"
    echo -e "\nPerformance Validation:"
    echo -e "  ${GREEN}✓${NC} Request batching with adaptive strategies"
    echo -e "  ${GREEN}✓${NC} Graceful degradation under high load"
    echo -e "  ${GREEN}✓${NC} Normal priority prevents AIManager contention"
    echo -e "  ${GREEN}✓${NC} Queue pressure stays below critical threshold"
else
    echo -e "${RED}✗ Some contention tests failed!${NC}"
    echo -e "\nPossible Issues:"
    echo -e "  ${YELLOW}•${NC} WorkerBudget coordination problems"
    echo -e "  ${YELLOW}•${NC} Worker starvation under heavy load"
    echo -e "  ${YELLOW}•${NC} Queue overflow or excessive pressure"
    echo -e "  ${YELLOW}•${NC} Rate limiting not working correctly"
fi

echo -e "\nResults saved to: ${CYAN}$RESULTS_FILE${NC}"
echo -e "${BLUE}======================================================${NC}"

exit $RESULT
