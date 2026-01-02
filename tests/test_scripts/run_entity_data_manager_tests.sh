#!/bin/bash
# EntityDataManager System Test Runner
# Copyright 2025 Hammer Forged Games

# Set up colored output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Process command line arguments
VERBOSE=false
RUN_ALL=true
RUN_EDM=false
RUN_BGSM=false

for arg in "$@"; do
  case $arg in
    --verbose)
      VERBOSE=true
      shift
      ;;
    --help)
      echo -e "${BLUE}EntityDataManager System Test Runner${NC}"
      echo -e "Usage: ./run_entity_data_manager_tests.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --verbose      Run tests with verbose output"
      echo -e "  --edm          Run only EntityDataManager tests"
      echo -e "  --bgsm         Run only BackgroundSimulationManager tests"
      echo -e "  --help         Show this help message"
      exit 0
      ;;
    --edm)
      RUN_ALL=false
      RUN_EDM=true
      shift
      ;;
    --bgsm)
      RUN_ALL=false
      RUN_BGSM=true
      shift
      ;;
  esac
done

echo -e "${BLUE}Running EntityDataManager System Tests${NC}"

# Get the directory where this script is located and find project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Track overall result
OVERALL_RESULT=0

# Function to run a single test
run_single_test() {
    local test_name=$1

    # Check if test executable exists
    local TEST_EXECUTABLE="$PROJECT_ROOT/bin/debug/$test_name"
    if [ ! -f "$TEST_EXECUTABLE" ]; then
        echo -e "${RED}Error: Test executable not found: $test_name${NC}"
        OVERALL_RESULT=1
        return
    fi

    # Set test options
    local TEST_OPTS="--log_level=test_suite --catch_system_errors=no"
    if [ "$VERBOSE" = true ]; then
        TEST_OPTS="--log_level=all --report_level=detailed"
    fi

    # Create test results directory
    mkdir -p "$PROJECT_ROOT/test_results"

    # Run the test
    echo -e "${BLUE}Running $test_name...${NC}"
    if [ "$VERBOSE" = true ]; then
        "$TEST_EXECUTABLE" $TEST_OPTS
    else
        "$TEST_EXECUTABLE" $TEST_OPTS > "$PROJECT_ROOT/test_results/${test_name}_output.txt" 2>&1
    fi

    local TEST_RESULT=$?

    if [ $TEST_RESULT -ne 0 ]; then
        echo -e "${RED}$test_name failed with exit code $TEST_RESULT${NC}"
        if [ "$VERBOSE" = false ]; then
            echo -e "${YELLOW}Output:${NC}"
            tail -20 "$PROJECT_ROOT/test_results/${test_name}_output.txt"
        fi
        OVERALL_RESULT=1
    else
        echo -e "${GREEN}$test_name completed successfully${NC}"
    fi
}

# Run selected tests
if [ "$RUN_ALL" = true ] || [ "$RUN_EDM" = true ]; then
    run_single_test "entity_data_manager_tests"
fi

if [ "$RUN_ALL" = true ] || [ "$RUN_BGSM" = true ]; then
    run_single_test "background_simulation_manager_tests"
fi

# Show summary
if [ $OVERALL_RESULT -ne 0 ]; then
    echo -e "\n${RED}Some EntityDataManager tests failed!${NC}"
    exit 1
else
    echo -e "\n${GREEN}All EntityDataManager tests completed successfully!${NC}"
    echo -e "${GREEN}✓ EntityDataManager tests${NC}"
    echo -e "${GREEN}✓ BackgroundSimulationManager tests${NC}"
    exit 0
fi
