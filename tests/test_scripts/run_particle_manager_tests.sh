#!/bin/bash

# Script to run all Particle Manager tests
# Copyright (c) 2025 Hammer Forged Games, MIT License

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
RUN_ALL=true
RUN_CORE=false
RUN_WEATHER=false
RUN_PERFORMANCE=false
RUN_THREADING=false

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
    --core)
      RUN_ALL=false
      RUN_CORE=true
      shift
      ;;
    --weather)
      RUN_ALL=false
      RUN_WEATHER=true
      shift
      ;;
    --performance)
      RUN_ALL=false
      RUN_PERFORMANCE=true
      shift
      ;;
    --threading)
      RUN_ALL=false
      RUN_THREADING=true
      shift
      ;;
    --help)
      echo -e "${BLUE}Particle Manager Test Runner${NC}"
      echo -e "Usage: ./run_particle_manager_tests.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --clean        Clean test artifacts before building"
      echo -e "  --clean-all    Remove entire build directory and rebuild"
      echo -e "  --verbose      Run tests with verbose output"
      echo -e "  --debug        Use debug build (default)"
      echo -e "  --release      Use release build"
      echo -e "  --core         Run only core functionality tests"
      echo -e "  --weather      Run only weather integration tests"
      echo -e "  --performance  Run only performance tests"
      echo -e "  --threading    Run only threading tests"
      echo -e "  --help         Show this help message"
      echo -e "\nTest Suites:"
      echo -e "  Core Tests:        Basic ParticleManager functionality (14 tests)"
      echo -e "  Weather Tests:     Weather integration and effects (9 tests)"
      echo -e "  Performance Tests: Performance benchmarks and scaling (8 tests)"
      echo -e "  Threading Tests:   Multi-threading safety (7 tests)"
      echo -e "\nExecution Time:"
      echo -e "  Core tests:        ~30 seconds"
      echo -e "  Weather tests:     ~45 seconds"
      echo -e "  Performance tests: ~2-3 minutes"
      echo -e "  Threading tests:   ~1-2 minutes"
      echo -e "  All tests:         ~4-6 minutes total"
      echo -e "\nExamples:"
      echo -e "  ./run_particle_manager_tests.sh              # Run all tests"
      echo -e "  ./run_particle_manager_tests.sh --core       # Quick core validation"
      echo -e "  ./run_particle_manager_tests.sh --weather    # Weather functionality only"
      echo -e "  ./run_particle_manager_tests.sh --verbose    # All tests with detailed output"
      exit 0
      ;;
  esac
done

echo -e "${BLUE}======================================================${NC}"
echo -e "${BLUE}           Particle Manager Test Runner             ${NC}"
echo -e "${BLUE}======================================================${NC}"

# Define test executables based on what to run
TEST_EXECUTABLES=()

if [ "$RUN_ALL" = true ] || [ "$RUN_CORE" = true ]; then
  TEST_EXECUTABLES+=("particle_manager_core_tests")
fi

if [ "$RUN_ALL" = true ] || [ "$RUN_WEATHER" = true ]; then
  TEST_EXECUTABLES+=("particle_manager_weather_tests")
fi

if [ "$RUN_ALL" = true ] || [ "$RUN_PERFORMANCE" = true ]; then
  TEST_EXECUTABLES+=("particle_manager_performance_tests")
fi

if [ "$RUN_ALL" = true ] || [ "$RUN_THREADING" = true ]; then
  TEST_EXECUTABLES+=("particle_manager_threading_tests")
fi

# Get the directory where this script is located and find project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Show execution plan
if [ "$RUN_ALL" = true ]; then
  echo -e "${YELLOW}Execution Plan: All Particle Manager tests (${#TEST_EXECUTABLES[@]} test suites)${NC}"
  echo -e "${YELLOW}Note: Performance and threading tests may take several minutes${NC}"
elif [ "$RUN_CORE" = true ]; then
  echo -e "${YELLOW}Execution Plan: Core functionality tests only${NC}"
  echo -e "${GREEN}Fast execution mode - basic ParticleManager validation${NC}"
elif [ "$RUN_WEATHER" = true ]; then
  echo -e "${YELLOW}Execution Plan: Weather integration tests only${NC}"
  echo -e "${GREEN}Testing weather effects and particle integration${NC}"
elif [ "$RUN_PERFORMANCE" = true ]; then
  echo -e "${YELLOW}Execution Plan: Performance tests only${NC}"
  echo -e "${YELLOW}Note: This will take 2-3 minutes to complete${NC}"
elif [ "$RUN_THREADING" = true ]; then
  echo -e "${YELLOW}Execution Plan: Threading tests only${NC}"
  echo -e "${YELLOW}Testing multi-threading safety and concurrency${NC}"
fi

echo -e "${CYAN}Found ${#TEST_EXECUTABLES[@]} test suites to run${NC}"
echo -e "${CYAN}Build type: ${BUILD_TYPE}${NC}"

# Track overall success
OVERALL_SUCCESS=true
PASSED_COUNT=0
FAILED_COUNT=0
TOTAL_COUNT=${#TEST_EXECUTABLES[@]}

# Create test results directory
mkdir -p "$PROJECT_ROOT/test_results/particle_manager"
COMBINED_RESULTS="$PROJECT_ROOT/test_results/particle_manager/all_particle_tests_results.txt"
echo "Particle Manager Tests Run $(date)" > "$COMBINED_RESULTS"

# Function to run a test executable
run_particle_test() {
  local exec_name=$1
  local test_type=""
  
  # Determine test type for better messaging
  case $exec_name in
    *core*)
      test_type="Core Functionality"
      ;;
    *weather*)
      test_type="Weather Integration"
      ;;
    *performance*)
      test_type="Performance Benchmarks"
      ;;
    *threading*)
      test_type="Threading Safety"
      ;;
  esac
  
  echo -e "\n${MAGENTA}=====================================================${NC}"
  echo -e "${CYAN}Running Particle Manager ${test_type} Tests${NC}"
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
  OUTPUT_FILE="$PROJECT_ROOT/test_results/particle_manager/${exec_name}_output.txt"
  
  echo -e "${YELLOW}Running with options: $TEST_OPTS${NC}"
  
  # Check for timeout command availability
  TIMEOUT_CMD=""
  if command -v timeout &> /dev/null; then
    TIMEOUT_CMD="timeout"
  elif command -v gtimeout &> /dev/null; then
    TIMEOUT_CMD="gtimeout"
  fi
  
  # Set timeout based on test type
  local timeout_duration="60s"
  if [[ "$exec_name" == *"performance"* ]]; then
    timeout_duration="300s"  # 5 minutes for performance tests
  elif [[ "$exec_name" == *"threading"* ]]; then
    timeout_duration="180s"  # 3 minutes for threading tests
  fi
  
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
  cp "$OUTPUT_FILE" "$PROJECT_ROOT/test_results/particle_manager/${exec_name}_output_${TIMESTAMP}.txt"
  
  # Extract performance metrics and test summary
  grep -E "time:|performance|TestCase|Running.*test cases|failures detected|No errors detected" "$OUTPUT_FILE" > "$PROJECT_ROOT/test_results/particle_manager/${exec_name}_summary.txt" || true
  
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
    echo -e "\n${GREEN}✓ ${test_type} tests completed successfully${NC}"
    
    # Extract test count information
    local test_count=$(grep -o "Running [0-9]\+ test cases" "$OUTPUT_FILE" | grep -o "[0-9]\+" | head -n 1)
    if [ -n "$test_count" ]; then
      echo -e "${GREEN}✓ All $test_count test cases passed${NC}"
    fi
    
    echo "PASSED: $exec_name" >> "$COMBINED_RESULTS"
    ((PASSED_COUNT++))
    return 0
  else
    echo -e "\n${RED}✗ ${test_type} tests failed${NC}"
    
    # Show failure summary
    echo -e "\n${YELLOW}Failure Summary:${NC}"
    grep -E "failure|FAILED|error.*in.*:" "$OUTPUT_FILE" | head -n 5 || echo -e "${YELLOW}No specific failure details found.${NC}"
    
    echo "FAILED: $exec_name (exit code: $test_result)" >> "$COMBINED_RESULTS"
    OVERALL_SUCCESS=false
    ((FAILED_COUNT++))
    return 1
  fi
}

# Run each test suite
for exec in "${TEST_EXECUTABLES[@]}"; do
  run_particle_test "$exec"
  
  # Add delay between test suites for resource cleanup
  if [[ "$exec" == *"performance"* ]] || [[ "$exec" == *"threading"* ]]; then
    echo -e "${YELLOW}Allowing time for resource cleanup...${NC}"
    sleep 3
  else
    sleep 1
  fi
done

# Print summary
echo -e "\n${BLUE}======================================================${NC}"
echo -e "${BLUE}            Particle Manager Test Summary            ${NC}"
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
  if [ "$RUN_ALL" = true ]; then
    echo -e "\n${GREEN}🎉 All Particle Manager tests completed successfully!${NC}"
    echo -e "${GREEN}✓ Core functionality: Verified${NC}"
    echo -e "${GREEN}✓ Weather integration: Verified${NC}"
    echo -e "${GREEN}✓ Performance benchmarks: Completed${NC}"
    echo -e "${GREEN}✓ Threading safety: Verified${NC}"
  elif [ "$RUN_CORE" = true ]; then
    echo -e "\n${GREEN}✅ Core Particle Manager tests completed successfully!${NC}"
    echo -e "${CYAN}To run weather tests: ./run_particle_manager_tests.sh --weather${NC}"
  elif [ "$RUN_WEATHER" = true ]; then
    echo -e "\n${GREEN}✅ Weather integration tests completed successfully!${NC}"
    echo -e "${CYAN}To run performance tests: ./run_particle_manager_tests.sh --performance${NC}"
  elif [ "$RUN_PERFORMANCE" = true ]; then
    echo -e "\n${GREEN}✅ Performance benchmarks completed successfully!${NC}"
    echo -e "${CYAN}To run threading tests: ./run_particle_manager_tests.sh --threading${NC}"
  elif [ "$RUN_THREADING" = true ]; then
    echo -e "\n${GREEN}✅ Threading safety tests completed successfully!${NC}"
    echo -e "${CYAN}To run all tests: ./run_particle_manager_tests.sh${NC}"
  fi
  echo -e "${BLUE}Test results saved to: ${YELLOW}test_results/particle_manager/${NC}"
  exit 0
else
  echo -e "\n${RED}❌ Some Particle Manager tests failed!${NC}"
  echo -e "${YELLOW}Please check the individual test results in test_results/particle_manager/${NC}"
  echo -e "${YELLOW}Combined results saved to: ${COMBINED_RESULTS}${NC}"
  exit 1
fi
