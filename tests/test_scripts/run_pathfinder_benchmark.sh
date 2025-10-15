#!/bin/bash

# Script to run pathfinder system performance benchmarks
# This script runs comprehensive pathfinding performance tests

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
      echo -e "${BLUE}Pathfinder System Benchmark Runner${NC}"
      echo -e "Usage: ./run_pathfinder_benchmark.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --verbose    Run benchmarks with verbose output"
      echo -e "  --help       Show this help message"
      echo -e "\nBenchmark Coverage:"
      echo -e "  - Immediate pathfinding performance across grid sizes"
      echo -e "  - Async pathfinding request throughput and latency"
      echo -e "  - Cache performance and hit rate analysis"
      echo -e "  - Path length vs computation time scaling"
      echo -e "  - Threading overhead vs benefits analysis"
      echo -e "  - Obstacle density impact on performance"
      echo -e "\nEstimated Runtime: 3-5 minutes"
      exit 0
      ;;
  esac
done

# Create results directory
mkdir -p "$PROJECT_DIR/test_results"
RESULTS_FILE="$PROJECT_DIR/test_results/pathfinder_benchmark_results.txt"
CSV_FILE="$PROJECT_DIR/test_results/pathfinder_benchmark.csv"

echo -e "${MAGENTA}======================================================${NC}"
echo -e "${MAGENTA}           Pathfinder System Benchmarks             ${NC}"
echo -e "${MAGENTA}======================================================${NC}"
echo -e "${YELLOW}Note: This benchmark may take several minutes to complete${NC}"

# Check if the test executable exists
TEST_EXECUTABLE="$PROJECT_DIR/bin/debug/pathfinder_benchmark"

if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo -e "${RED}Benchmark executable not found: $TEST_EXECUTABLE${NC}"
    echo -e "${YELLOW}Make sure you have built the project with tests enabled.${NC}"
    echo -e "Run: ${CYAN}cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build${NC}"
    exit 1
fi

# Make sure the executable has proper permissions
chmod +x "$TEST_EXECUTABLE"

# Run the benchmarks
echo -e "${CYAN}Starting pathfinder system benchmarks...${NC}"
echo "Pathfinder System Benchmarks - $(date)" > "$RESULTS_FILE"

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

# Report results
echo -e "\n${MAGENTA}======================================================${NC}"
if [ $RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ All pathfinder system benchmarks completed successfully!${NC}"
    echo -e "\nBenchmark Coverage:"
    echo -e "  ${GREEN}✓${NC} Immediate pathfinding performance across grid sizes"
    echo -e "  ${GREEN}✓${NC} Async pathfinding throughput and latency analysis"
    echo -e "  ${GREEN}✓${NC} Cache performance and hit rate optimization"
    echo -e "  ${GREEN}✓${NC} Path length vs computation time scaling"
    echo -e "  ${GREEN}✓${NC} Threading performance comparison"

    if [ -f "$CSV_FILE" ]; then
        echo -e "\n${CYAN}Detailed CSV results generated: $CSV_FILE${NC}"
        echo -e "${CYAN}Import this file into spreadsheet software for analysis${NC}"
    fi
else
    echo -e "${RED}✗ Some benchmarks failed or encountered issues${NC}"
    echo -e "${YELLOW}Check the detailed results in: $RESULTS_FILE${NC}"
fi

echo -e "\n${CYAN}Benchmark results saved to: $RESULTS_FILE${NC}"
echo -e "${MAGENTA}======================================================${NC}"

# Performance analysis and recommendations
if [ $RESULT -eq 0 ]; then
    echo -e "\n${BLUE}Performance Analysis:${NC}"
    echo -e "${CYAN}Use the results to identify:${NC}"
    echo -e "  • Optimal grid sizes for your target frame rates"
    echo -e "  • Threading benefits vs overhead for your workload"
    echo -e "  • Cache effectiveness for repeated pathfinding patterns"
    echo -e "  • Performance scaling with path length and complexity"
    echo -e "\n${YELLOW}Recommended Performance Targets:${NC}"
    echo -e "  • Immediate pathfinding: < 20ms per request"
    echo -e "  • Async throughput: > 100 paths/second"
    echo -e "  • Cache speedup: > 2x for repeated paths"
    echo -e "  • Success rate: > 90% for reasonable requests"
    echo -e "  • Frame budget: < 16.67ms total (60 FPS)"
fi

exit $RESULT