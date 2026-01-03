#!/bin/bash

# Script to run collision system scaling benchmarks
# Tests SAP (Sweep-and-Prune) for MM and Spatial Hash for MS detection

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
      echo -e "${BLUE}Collision Scaling Benchmark Runner${NC}"
      echo -e "Usage: ./run_collision_scaling_benchmark.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --verbose    Run benchmarks with verbose output"
      echo -e "  --help       Show this help message"
      echo -e "\nBenchmark Coverage:"
      echo -e "  - MM Scaling: Sweep-and-Prune effectiveness (100-2000 movables)"
      echo -e "  - MS Scaling: Spatial Hash performance (100-5000 statics)"
      echo -e "  - Combined Scaling: Real-world entity ratios"
      echo -e "  - Entity Density: Clustered vs spread distributions"
      echo -e "\nEstimated Runtime: 1-2 minutes"
      exit 0
      ;;
  esac
done

# Create results directory
mkdir -p "$PROJECT_DIR/test_results"
RESULTS_FILE="$PROJECT_DIR/test_results/collision_scaling_benchmark_$(date +%Y%m%d_%H%M%S).txt"
CURRENT_FILE="$PROJECT_DIR/test_results/collision_scaling_current.txt"

echo -e "${MAGENTA}======================================================${NC}"
echo -e "${MAGENTA}         Collision Scaling Benchmark                  ${NC}"
echo -e "${MAGENTA}======================================================${NC}"
echo -e "${YELLOW}Testing SAP for MM and Spatial Hash for MS detection${NC}"

# Check if the test executable exists
TEST_EXECUTABLE="$PROJECT_DIR/bin/debug/collision_scaling_benchmark"

if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo -e "${RED}Benchmark executable not found: $TEST_EXECUTABLE${NC}"
    echo -e "${YELLOW}Make sure you have built the project with tests enabled.${NC}"
    echo -e "Run: ${CYAN}cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build${NC}"
    exit 1
fi

# Make sure the executable has proper permissions
chmod +x "$TEST_EXECUTABLE"

# Run the benchmarks
echo -e "${CYAN}Starting collision scaling benchmarks...${NC}"
echo "Collision Scaling Benchmark - $(date)" > "$RESULTS_FILE"

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
    echo -e "${GREEN}All collision scaling benchmarks completed successfully!${NC}"
    echo -e "\nBenchmark Coverage:"
    echo -e "  ${GREEN}✓${NC} MM Scaling (Sweep-and-Prune)"
    echo -e "  ${GREEN}✓${NC} MS Scaling (Spatial Hash)"
    echo -e "  ${GREEN}✓${NC} Combined Scaling (Real-world ratios)"
    echo -e "  ${GREEN}✓${NC} Entity Density (Distribution effects)"
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
    echo -e "  • MM (500 movables): < 0.5ms"
    echo -e "  • MS (1000 statics): < 0.2ms"
    echo -e "  • Combined (1500 entities): < 0.8ms"
    echo -e "  • Sub-quadratic scaling confirmed"
fi

exit $RESULT
