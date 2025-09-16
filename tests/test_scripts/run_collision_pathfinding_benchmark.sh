#!/bin/bash

# Script to run collision and pathfinding performance benchmarks
# This script runs comprehensive performance tests for both systems

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
      echo -e "${BLUE}Collision & Pathfinding Benchmark Runner${NC}"
      echo -e "Usage: ./run_collision_pathfinding_benchmark.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --verbose    Run benchmarks with verbose output"
      echo -e "  --help       Show this help message"
      echo -e "\nBenchmark Coverage:"
      echo -e "  - SpatialHash insertion performance (100-10K entities)"
      echo -e "  - SpatialHash query performance with various entity densities"
      echo -e "  - SpatialHash update performance during movement"
      echo -e "  - Pathfinding grid performance (50x50 to 200x200)"
      echo -e "  - Weighted pathfinding with avoidance areas"
      echo -e "  - Iteration limit impact on pathfinding performance"
      echo -e "\nEstimated Runtime: 2-5 minutes"
      exit 0
      ;;
  esac
done

# Create results directory
mkdir -p "$PROJECT_DIR/test_results"
RESULTS_FILE="$PROJECT_DIR/test_results/collision_pathfinding_benchmark_results.txt"
CSV_FILE="$PROJECT_DIR/test_results/collision_pathfinding_benchmark.csv"

echo -e "${MAGENTA}======================================================${NC}"
echo -e "${MAGENTA}      Collision & Pathfinding Benchmarks           ${NC}"
echo -e "${MAGENTA}======================================================${NC}"
echo -e "${YELLOW}Note: This benchmark may take several minutes to complete${NC}"

# Check if the test executable exists
TEST_EXECUTABLE="$PROJECT_DIR/bin/debug/collision_pathfinding_benchmark"

if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo -e "${RED}Benchmark executable not found: $TEST_EXECUTABLE${NC}"
    echo -e "${YELLOW}Make sure you have built the project with tests enabled.${NC}"
    echo -e "Run: ${CYAN}cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build${NC}"
    exit 1
fi

# Make sure the executable has proper permissions
chmod +x "$TEST_EXECUTABLE"

# Run the benchmarks
echo -e "${CYAN}Starting collision and pathfinding benchmarks...${NC}"
echo "Collision & Pathfinding Benchmarks - $(date)" > "$RESULTS_FILE"

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
    echo -e "${GREEN}✓ All collision and pathfinding benchmarks completed successfully!${NC}"
    echo -e "\nBenchmark Coverage:"
    echo -e "  ${GREEN}✓${NC} SpatialHash insertion performance scaling"
    echo -e "  ${GREEN}✓${NC} SpatialHash query performance with entity density"
    echo -e "  ${GREEN}✓${NC} SpatialHash update performance during movement"
    echo -e "  ${GREEN}✓${NC} Pathfinding grid performance across sizes"
    echo -e "  ${GREEN}✓${NC} Weighted pathfinding with cost areas"
    echo -e "  ${GREEN}✓${NC} Iteration limit impact analysis"
    
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
    echo -e "${CYAN}Use the CSV file to identify:${NC}"
    echo -e "  • Optimal SpatialHash cell sizes for your entity density"
    echo -e "  • Entity count limits for maintaining target frame rates"
    echo -e "  • Pathfinding grid sizes suitable for real-time performance"
    echo -e "  • Impact of iteration limits on pathfinding success rates"
    echo -e "\n${YELLOW}Recommended Performance Targets:${NC}"
    echo -e "  • SpatialHash insertion: < 50μs per entity"
    echo -e "  • SpatialHash query: < 100μs per query"
    echo -e "  • SpatialHash update: < 75μs per update"
    echo -e "  • Pathfinding (100x100): < 10ms per request"
    echo -e "  • Pathfinding (150x150): < 20ms per request"
fi

exit $RESULT