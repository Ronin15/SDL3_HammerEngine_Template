#!/bin/bash

# Script to run Particle Manager performance benchmarks
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
      echo -e "${BLUE}Particle Manager Performance Benchmark${NC}"
      echo -e "Usage: ./run_particle_manager_benchmark.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --clean        Clean test artifacts before building"
      echo -e "  --clean-all    Remove entire build directory and rebuild"
      echo -e "  --verbose      Run tests with verbose output"
      echo -e "  --debug        Use debug build (default)"
      echo -e "  --release      Use release build"
      echo -e "  --help         Show this help message"
      echo -e "\nBenchmark Tests:"
      echo -e "  Performance Tests: Performance benchmarks and scaling (8 tests)"
      echo -e "\nExecution Time:"
      echo -e "  Performance tests: ~2-3 minutes"
      echo -e "\nExamples:"
      echo -e "  ./run_particle_manager_benchmark.sh              # Run performance benchmarks"
      echo -e "  ./run_particle_manager_benchmark.sh --verbose    # Detailed benchmark output"
      exit 0
      ;;
  esac
done

echo -e "${BLUE}======================================================${NC}"
echo -e "${BLUE}    Particle Manager Performance Benchmark          ${NC}"
echo -e "${BLUE}======================================================${NC}"

# Define test executable
TEST_EXECUTABLE="particle_manager_performance_tests"

# Get the directory where this script is located and find project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo -e "${YELLOW}Execution Plan: Particle Manager performance benchmarks${NC}"
echo -e "${YELLOW}Note: This will take 2-3 minutes to complete${NC}"
echo -e "${CYAN}Build type: ${BUILD_TYPE}${NC}"

# Track success
OVERALL_SUCCESS=true

# Create test results directory
mkdir -p "$PROJECT_ROOT/test_results/particle_manager"
RESULTS_FILE="$PROJECT_ROOT/test_results/particle_manager/performance_benchmark_results.txt"
echo "Particle Manager Performance Benchmark $(date)" > "$RESULTS_FILE"

# Determine the correct path to the test executable
if [ "$BUILD_TYPE" = "Debug" ]; then
  TEST_PATH="$PROJECT_ROOT/bin/debug/$TEST_EXECUTABLE"
else
  TEST_PATH="$PROJECT_ROOT/bin/release/$TEST_EXECUTABLE"
fi

echo -e "\n${MAGENTA}=====================================================${NC}"
echo -e "${CYAN}Running Particle Manager Performance Benchmarks${NC}"
echo -e "${YELLOW}Test Suite: ${TEST_EXECUTABLE}${NC}"
echo -e "${MAGENTA}=====================================================${NC}"

# Check if test executable exists
if [ ! -f "$TEST_PATH" ]; then
  echo -e "${RED}Test executable not found at $TEST_PATH${NC}"
  echo -e "${YELLOW}Searching for test executable...${NC}"
  FOUND_EXECUTABLE=$(find "$PROJECT_ROOT" -name "$TEST_EXECUTABLE" -type f -executable | head -n 1)
  if [ -n "$FOUND_EXECUTABLE" ]; then
    TEST_PATH="$FOUND_EXECUTABLE"
    echo -e "${GREEN}Found test executable at $TEST_PATH${NC}"
  else
    echo -e "${RED}Could not find test executable!${NC}"
    echo "FAILED: $TEST_EXECUTABLE - executable not found" >> "$RESULTS_FILE"
    exit 1
  fi
fi

# Set test command options
TEST_OPTS="--log_level=test_suite --catch_system_errors=no"
if [ "$VERBOSE" = true ]; then
  TEST_OPTS="--log_level=all --report_level=detailed"
fi

# Create output files with timestamps
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
OUTPUT_FILE="$PROJECT_ROOT/test_results/particle_manager/${TEST_EXECUTABLE}_output.txt"
TIMESTAMPED_FILE="$PROJECT_ROOT/test_results/particle_benchmark_${TIMESTAMP}.txt"
METRICS_FILE="$PROJECT_ROOT/test_results/particle_benchmark_performance_metrics.txt"
SUMMARY_FILE="$PROJECT_ROOT/test_results/particle_benchmark_summary_${TIMESTAMP}.txt"
CSV_FILE="$PROJECT_ROOT/test_results/particle_benchmark.csv"

echo -e "${YELLOW}Running with options: $TEST_OPTS${NC}"

# Check for timeout command availability
TIMEOUT_CMD=""
if command -v timeout &> /dev/null; then
  TIMEOUT_CMD="timeout"
elif command -v gtimeout &> /dev/null; then
  TIMEOUT_CMD="gtimeout"
fi

# Set timeout for performance tests
timeout_duration="300s"  # 5 minutes for performance tests

# Run the tests with timeout protection
echo "============ PARTICLE MANAGER BENCHMARK START ============" > "$OUTPUT_FILE"
echo "Date: $(date)" >> "$OUTPUT_FILE"
echo "Build type: $BUILD_TYPE" >> "$OUTPUT_FILE"
echo "Command: $TEST_PATH $TEST_OPTS" >> "$OUTPUT_FILE"
echo "==========================================================" >> "$OUTPUT_FILE"
echo >> "$OUTPUT_FILE"

test_result=0
if [ -n "$TIMEOUT_CMD" ]; then
  if [ "$VERBOSE" = true ]; then
    $TIMEOUT_CMD $timeout_duration "$TEST_PATH" $TEST_OPTS 2>&1 | tee -a "$OUTPUT_FILE"
    test_result=$?
  else
    $TIMEOUT_CMD $timeout_duration "$TEST_PATH" $TEST_OPTS 2>&1 | tee -a "$OUTPUT_FILE"
    test_result=$?
  fi
else
  if [ "$VERBOSE" = true ]; then
    "$TEST_PATH" $TEST_OPTS 2>&1 | tee -a "$OUTPUT_FILE"
    test_result=$?
  else
    "$TEST_PATH" $TEST_OPTS 2>&1 | tee -a "$OUTPUT_FILE"
    test_result=$?
  fi
fi

echo >> "$OUTPUT_FILE"
echo "============ PARTICLE MANAGER BENCHMARK END ============" >> "$OUTPUT_FILE"
echo "Date: $(date)" >> "$OUTPUT_FILE"
echo "Exit code: $test_result" >> "$OUTPUT_FILE"
echo "========================================================" >> "$OUTPUT_FILE"

echo -e "${BLUE}====================================${NC}"

# Save timestamped copy to main test_results directory
cp "$OUTPUT_FILE" "$TIMESTAMPED_FILE"
cp "$OUTPUT_FILE" "$PROJECT_ROOT/test_results/particle_manager/${TEST_EXECUTABLE}_output_${TIMESTAMP}.txt"

# Extract performance metrics and test summary
grep -E "time:|performance|ms|TestCase|Running.*test cases|failures detected|No errors detected|particles=" "$OUTPUT_FILE" > "$PROJECT_ROOT/test_results/particle_manager/${TEST_EXECUTABLE}_summary.txt" || true

# Handle timeout scenario
if [ -n "$TIMEOUT_CMD" ] && [ $test_result -eq 124 ]; then
  echo -e "${RED}⚠️ Benchmark execution timed out after $timeout_duration!${NC}"
  echo "FAILED: $TEST_EXECUTABLE - timed out after $timeout_duration" >> "$RESULTS_FILE"
  exit 1
fi

# Extract performance metrics
echo -e "${YELLOW}Extracting performance metrics...${NC}"
echo "============ PARTICLE MANAGER PERFORMANCE METRICS ============" > "$METRICS_FILE"
echo "Date: $(date)" >> "$METRICS_FILE"
echo "Build type: $BUILD_TYPE" >> "$METRICS_FILE"
echo "==============================================================" >> "$METRICS_FILE"
echo >> "$METRICS_FILE"

# Extract key performance data
grep -E "Update time:|particles|time:|ms|Created|Initial particle count|Final particle count|Cleanup time|HighCountBench|update_avg_ms" "$OUTPUT_FILE" >> "$METRICS_FILE" || true

echo >> "$METRICS_FILE"
echo "============ END OF METRICS ============" >> "$METRICS_FILE"

# Create CSV file for trackable benchmark data
echo -e "${YELLOW}Generating CSV data for tracking...${NC}"
echo "TestName,ParticleCount,UpdateTimeMs,ThroughputMetric,AdditionalInfo" > "$CSV_FILE"

# Extract data from benchmark tests and create CSV entries
# TestUpdatePerformance1000Particles
PERF_1000=$(grep -A 5 "TestUpdatePerformance1000Particles" "$OUTPUT_FILE" | grep "Update time:" | awk '{print $3}')
PARTICLES_1000=$(grep -A 5 "TestUpdatePerformance1000Particles" "$OUTPUT_FILE" | grep "Testing update performance with" | awk '{print $5}')
if [ -n "$PERF_1000" ] && [ -n "$PARTICLES_1000" ]; then
  echo "UpdatePerformance,${PARTICLES_1000},${PERF_1000},N/A,1K particle test" >> "$CSV_FILE"
fi

# TestUpdatePerformance5000Particles
PERF_5000=$(grep -A 5 "TestUpdatePerformance5000Particles" "$OUTPUT_FILE" | grep "Update time:" | awk '{print $3}')
PARTICLES_5000=$(grep -A 5 "TestUpdatePerformance5000Particles" "$OUTPUT_FILE" | grep "Testing update performance with" | awk '{print $5}')
if [ -n "$PERF_5000" ] && [ -n "$PARTICLES_5000" ]; then
  echo "UpdatePerformance,${PARTICLES_5000},${PERF_5000},N/A,5K particle test" >> "$CSV_FILE"
fi

# TestParticleCreationThroughput
CREATION_TIME=$(grep -A 10 "TestParticleCreationThroughput" "$OUTPUT_FILE" | grep "Time to create" | grep -o "[0-9.]*ms" | grep -o "[0-9.]*")
NUM_EFFECTS=$(grep -A 10 "TestParticleCreationThroughput" "$OUTPUT_FILE" | grep "Time to create" | grep -o "[0-9]* effects" | grep -o "[0-9]*")
if [ -n "$CREATION_TIME" ] && [ -n "$NUM_EFFECTS" ]; then
  echo "CreationThroughput,N/A,${CREATION_TIME},${NUM_EFFECTS},Effect creation rate" >> "$CSV_FILE"
fi

# TestSustainedPerformance
SUSTAINED_AVG=$(grep -A 10 "TestSustainedPerformance" "$OUTPUT_FILE" | grep "Average:" | awk '{print $2}' | grep -o "[0-9.]*")
SUSTAINED_MAX=$(grep -A 10 "TestSustainedPerformance" "$OUTPUT_FILE" | grep "Max:" | awk '{print $2}' | grep -o "[0-9.]*")
if [ -n "$SUSTAINED_AVG" ]; then
  echo "SustainedPerformance,1500,${SUSTAINED_AVG},${SUSTAINED_MAX},60 frames average/max" >> "$CSV_FILE"
fi

# HighCountBenchmarks
HIGH_10K=$(grep "HighCountBench: target=10000" "$OUTPUT_FILE" -A 1 | grep "update_avg_ms" | awk -F'=' '{print $2}')
HIGH_25K=$(grep "HighCountBench: target=25000" "$OUTPUT_FILE" -A 1 | grep "update_avg_ms" | awk -F'=' '{print $2}')
HIGH_50K=$(grep "HighCountBench: target=50000" "$OUTPUT_FILE" -A 1 | grep "update_avg_ms" | awk -F'=' '{print $2}')

if [ -n "$HIGH_10K" ]; then
  ACTUAL_10K=$(grep "HighCountBench: target=10000" "$OUTPUT_FILE" | awk -F'actual=' '{print $2}' | awk '{print $1}' | tr -d ',')
  echo "HighCountBench,${ACTUAL_10K},${HIGH_10K},N/A,10K target" >> "$CSV_FILE"
fi
if [ -n "$HIGH_25K" ]; then
  ACTUAL_25K=$(grep "HighCountBench: target=25000" "$OUTPUT_FILE" | awk -F'actual=' '{print $2}' | awk '{print $1}' | tr -d ',')
  echo "HighCountBench,${ACTUAL_25K},${HIGH_25K},N/A,25K target" >> "$CSV_FILE"
fi
if [ -n "$HIGH_50K" ]; then
  ACTUAL_50K=$(grep "HighCountBench: target=50000" "$OUTPUT_FILE" | awk -F'actual=' '{print $2}' | awk '{print $1}' | tr -d ',')
  echo "HighCountBench,${ACTUAL_50K},${HIGH_50K},N/A,50K target" >> "$CSV_FILE"
fi

# Create summary file
echo "============ PARTICLE MANAGER BENCHMARK SUMMARY ============" > "$SUMMARY_FILE"
echo "Date: $(date)" >> "$SUMMARY_FILE"
echo "Build type: $BUILD_TYPE" >> "$SUMMARY_FILE"
echo "Exit code: $test_result" >> "$SUMMARY_FILE"
echo "============================================================" >> "$SUMMARY_FILE"
echo >> "$SUMMARY_FILE"

echo "Key Performance Metrics:" >> "$SUMMARY_FILE"
if [ -n "$PERF_1000" ] && [ -n "$PARTICLES_1000" ]; then
  echo "  1K particles update: ${PERF_1000}ms (${PARTICLES_1000} particles)" >> "$SUMMARY_FILE"
fi
if [ -n "$PERF_5000" ] && [ -n "$PARTICLES_5000" ]; then
  echo "  5K particles update: ${PERF_5000}ms (${PARTICLES_5000} particles)" >> "$SUMMARY_FILE"
fi
if [ -n "$SUSTAINED_AVG" ]; then
  echo "  Sustained performance avg: ${SUSTAINED_AVG}ms" >> "$SUMMARY_FILE"
  echo "  Sustained performance max: ${SUSTAINED_MAX}ms" >> "$SUMMARY_FILE"
fi
if [ -n "$HIGH_10K" ]; then
  echo "  High count (10K target): ${HIGH_10K}ms" >> "$SUMMARY_FILE"
fi
if [ -n "$HIGH_25K" ]; then
  echo "  High count (25K target): ${HIGH_25K}ms" >> "$SUMMARY_FILE"
fi
if [ -n "$HIGH_50K" ]; then
  echo "  High count (50K target): ${HIGH_50K}ms" >> "$SUMMARY_FILE"
fi

echo >> "$SUMMARY_FILE"

# Check test results
if [ $test_result -eq 0 ] && ! grep -q "failure\|test cases failed\|errors detected.*[1-9]" "$OUTPUT_FILE"; then
  echo -e "\n${GREEN}✓ Performance benchmarks completed successfully${NC}"

  # Extract test count information
  test_count=$(grep -o "Running [0-9]\+ test cases" "$OUTPUT_FILE" | grep -o "[0-9]\+" | head -n 1)
  if [ -n "$test_count" ]; then
    echo -e "${GREEN}✓ All $test_count benchmark tests passed${NC}"
  fi

  echo "PASSED: $TEST_EXECUTABLE" >> "$RESULTS_FILE"
  echo "Status: ✅ Benchmark completed successfully" >> "$SUMMARY_FILE"
  OVERALL_SUCCESS=true
else
  echo -e "\n${RED}✗ Performance benchmarks failed${NC}"

  # Show failure summary
  echo -e "\n${YELLOW}Failure Summary:${NC}"
  grep -E "failure|FAILED|error.*in.*:" "$OUTPUT_FILE" | head -n 5 || echo -e "${YELLOW}No specific failure details found.${NC}"

  echo "FAILED: $TEST_EXECUTABLE (exit code: $test_result)" >> "$RESULTS_FILE"
  echo "Status: ❌ Benchmark failed" >> "$SUMMARY_FILE"
  OVERALL_SUCCESS=false
fi

echo >> "$SUMMARY_FILE"
echo "============ END OF SUMMARY ============" >> "$SUMMARY_FILE"

# Print summary
echo -e "\n${BLUE}======================================================${NC}"
echo -e "${BLUE}         Performance Benchmark Summary              ${NC}"
echo -e "${BLUE}======================================================${NC}"

# Display performance summary
if [ -f "$METRICS_FILE" ]; then
  echo -e "\n${BLUE}Performance Metrics Summary:${NC}"
  cat "$SUMMARY_FILE"
fi

# Display quick performance summary
echo -e "\n${BLUE}Quick Performance Summary:${NC}"
if [ -n "$PERF_1000" ]; then
  echo -e "  1K particles: ${GREEN}${PERF_1000}ms${NC}"
fi
if [ -n "$PERF_5000" ]; then
  echo -e "  5K particles: ${GREEN}${PERF_5000}ms${NC}"
fi
if [ -n "$SUSTAINED_AVG" ]; then
  echo -e "  Sustained avg: ${GREEN}${SUSTAINED_AVG}ms${NC} (max: ${SUSTAINED_MAX}ms)"
fi
if [ -n "$HIGH_10K" ]; then
  echo -e "  High count (10K): ${GREEN}${HIGH_10K}ms${NC}"
fi

# Display CSV info
if [ -f "$CSV_FILE" ]; then
  echo -e "\n${CYAN}CSV data generated for tracking performance over time${NC}"
  echo -e "${CYAN}CSV file: ${YELLOW}$CSV_FILE${NC}"
  CSV_LINES=$(wc -l < "$CSV_FILE")
  echo -e "${CYAN}Data points captured: ${GREEN}$((CSV_LINES - 1))${NC}"
fi

# Exit with appropriate status code
if [ "$OVERALL_SUCCESS" = true ]; then
  echo -e "\n${GREEN}🎉 Particle Manager performance benchmarks completed successfully!${NC}"
  echo -e "\n${GREEN}Results saved to:${NC}"
  echo -e "  - Timestamped log: ${BLUE}$TIMESTAMPED_FILE${NC}"
  echo -e "  - Performance metrics: ${BLUE}$METRICS_FILE${NC}"
  echo -e "  - Summary: ${BLUE}$SUMMARY_FILE${NC}"
  echo -e "  - CSV data: ${BLUE}$CSV_FILE${NC}"
  echo -e "  - Local copy: ${BLUE}test_results/particle_manager/${NC}"
  exit 0
else
  echo -e "\n${RED}❌ Particle Manager performance benchmarks failed!${NC}"
  echo -e "${YELLOW}Please check the benchmark results:${NC}"
  echo -e "  - Full log: ${YELLOW}$TIMESTAMPED_FILE${NC}"
  echo -e "  - Results: ${YELLOW}$RESULTS_FILE${NC}"
  exit 1
fi
