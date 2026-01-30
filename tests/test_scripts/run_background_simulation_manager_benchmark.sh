#!/bin/bash

# Script to run BackgroundSimulationManager benchmarks
# Tests background entity scaling, threading threshold detection, and WorkerBudget adaptive tuning

# Set up colored output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
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
      echo -e "${BLUE}BackgroundSimulationManager Benchmark Runner${NC}"
      echo -e "Usage: ./run_background_simulation_manager_benchmark.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --verbose    Run benchmarks with verbose output"
      echo -e "  --help       Show this help message"
      echo -e "\nBenchmark Coverage:"
      echo -e "  - Entity Scaling: 100 to 10,000 background entities"
      echo -e "  - Threading Threshold: Single vs multi-threaded crossover detection"
      echo -e "  - Adaptive Tuning: WorkerBudget batch sizing and throughput tracking"
      echo -e "\nEstimated Runtime: 3-5 minutes"
      exit 0
      ;;
  esac
done

# Create results directory
mkdir -p "$PROJECT_DIR/test_results"
RESULTS_FILE="$PROJECT_DIR/test_results/background_simulation_benchmark_$(date +%Y%m%d_%H%M%S).txt"
CURRENT_FILE="$PROJECT_DIR/test_results/background_simulation_benchmark_current.txt"

echo -e "${MAGENTA}======================================================${NC}"
echo -e "${MAGENTA}     BackgroundSimulationManager Benchmark            ${NC}"
echo -e "${MAGENTA}======================================================${NC}"
echo -e "${YELLOW}Testing background entity processing and tier-based simulation${NC}"

# Check if the test executable exists
TEST_EXECUTABLE="$PROJECT_DIR/bin/debug/background_simulation_manager_benchmark"

if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo -e "${RED}Benchmark executable not found: $TEST_EXECUTABLE${NC}"
    echo -e "${YELLOW}Make sure you have built the project with tests enabled.${NC}"
    echo -e "Run: ${CYAN}cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build${NC}"
    exit 1
fi

# Make sure the executable has proper permissions
chmod +x "$TEST_EXECUTABLE"

# Run the benchmarks
echo -e "${CYAN}Starting BackgroundSimulationManager benchmarks...${NC}"
echo "BackgroundSimulationManager Benchmark - $(date)" > "$RESULTS_FILE"

if [ "$VERBOSE" = true ]; then
    echo -e "${YELLOW}Verbose mode enabled${NC}"
    $TEST_EXECUTABLE --log_level=all 2>&1 | tee -a "$RESULTS_FILE"
    RESULT=${PIPESTATUS[0]}
else
    $TEST_EXECUTABLE --log_level=test_suite 2>&1 | tee -a "$RESULTS_FILE"
    RESULT=${PIPESTATUS[0]}
fi

echo "" >> "$RESULTS_FILE"
echo "Benchmark completed at: $(date)" >> "$RESULTS_FILE"
echo "Exit code: $RESULT" >> "$RESULTS_FILE"

# Copy to current file for regression comparison
cp "$RESULTS_FILE" "$CURRENT_FILE"

# Report results
echo -e "\n${MAGENTA}======================================================${NC}"
if [ $RESULT -eq 0 ]; then
    echo -e "${GREEN}All BackgroundSimulationManager benchmarks completed successfully!${NC}"
    echo -e "\nBenchmark Coverage:"
    echo -e "  ${GREEN}✓${NC} Entity Scaling (100-10K entities)"
    echo -e "  ${GREEN}✓${NC} Threading Threshold Detection"
    echo -e "  ${GREEN}✓${NC} WorkerBudget Adaptive Tuning"
else
    echo -e "${RED}Some benchmarks failed or encountered issues${NC}"
    echo -e "${YELLOW}Check the detailed results in: $RESULTS_FILE${NC}"
fi

echo -e "\n${CYAN}Results saved to: $RESULTS_FILE${NC}"
echo -e "${CYAN}Current results: $CURRENT_FILE${NC}"
echo -e "${MAGENTA}======================================================${NC}"

# Performance targets
if [ $RESULT -eq 0 ]; then
    echo -e "\n${BLUE}Performance Targets:${NC}"
    echo -e "  • 1000 background entities: < 1ms per update"
    echo -e "  • 5000 background entities: < 3ms per update"
    echo -e "  • 10000 background entities: < 6ms per update"
    echo -e "  • Batch sizing convergence verified"
    echo -e "  • Throughput tracking active"
fi

exit $RESULT
