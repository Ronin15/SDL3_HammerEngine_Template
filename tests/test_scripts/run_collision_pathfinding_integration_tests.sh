#!/bin/bash

# Script to run collision pathfinding integration tests
# This script runs comprehensive integration tests for collision and pathfinding systems

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
      echo -e "${BLUE}Collision Pathfinding Integration Tests Runner${NC}"
      echo -e "Usage: ./run_collision_pathfinding_integration_tests.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --verbose    Enable verbose output"
      echo -e "  --help       Show this help message"
      echo -e "\nThis script tests the integration between collision detection"
      echo -e "and pathfinding systems, including:"
      echo -e "  • Obstacle avoidance pathfinding"
      echo -e "  • Dynamic obstacle integration"
      echo -e "  • Event-driven cache invalidation"
      echo -e "  • Concurrent operations stress testing"
      echo -e "  • Performance under load"
      echo -e "  • Collision layer integration"
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
RESULTS_FILE="$PROJECT_DIR/test_results/collision_pathfinding_integration_tests_results.txt"

echo -e "${BLUE}======================================================${NC}"
echo -e "${BLUE}      Collision Pathfinding Integration Tests        ${NC}"
echo -e "${BLUE}======================================================${NC}"

# Check if the test executable exists
TEST_EXECUTABLE="$PROJECT_DIR/bin/debug/collision_pathfinding_integration_tests"

if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo -e "${RED}Test executable not found: $TEST_EXECUTABLE${NC}"
    echo -e "${YELLOW}Make sure you have built the project with tests enabled.${NC}"
    echo -e "Run: ${CYAN}cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build${NC}"
    exit 1
fi

# Make sure the executable has proper permissions
chmod +x "$TEST_EXECUTABLE"

# Run the tests
echo -e "${CYAN}Running collision pathfinding integration tests...${NC}"
echo "Collision Pathfinding Integration Tests - $(date)" > "$RESULTS_FILE"

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
    echo -e "${GREEN}✓ All collision pathfinding integration tests passed successfully!${NC}"
    echo -e "\nTest Coverage:"
    echo -e "  ${GREEN}✓${NC} Obstacle avoidance pathfinding"
    echo -e "  ${GREEN}✓${NC} Dynamic obstacle integration"
    echo -e "  ${GREEN}✓${NC} Event-driven cache invalidation"
    echo -e "  ${GREEN}✓${NC} Concurrent collision and pathfinding operations"
    echo -e "  ${GREEN}✓${NC} Performance under load testing"
    echo -e "  ${GREEN}✓${NC} Collision layer pathfinding interaction"
    echo -e "\nIntegration Validation:"
    echo -e "  ${GREEN}✓${NC} Real-world collision detection integration"
    echo -e "  ${GREEN}✓${NC} Thread safety with concurrent operations"
    echo -e "  ${GREEN}✓${NC} Event system integration"
    echo -e "  ${GREEN}✓${NC} System performance under stress"
else
    echo -e "${RED}✗ Some integration tests failed!${NC}"
    echo -e "\nPossible Issues:"
    echo -e "  ${YELLOW}•${NC} System initialization problems"
    echo -e "  ${YELLOW}•${NC} Threading or concurrency issues"
    echo -e "  ${YELLOW}•${NC} Event system integration problems"
    echo -e "  ${YELLOW}•${NC} Performance degradation"
fi

echo -e "\nResults saved to: ${CYAN}$RESULTS_FILE${NC}"
echo -e "${BLUE}======================================================${NC}"

exit $RESULT