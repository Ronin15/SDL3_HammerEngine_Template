#!/bin/bash

# Script to run Adaptive Threading Analysis benchmarks
# Measures single vs multi-threaded throughput to determine optimal threading thresholds

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
      echo -e "${BLUE}Adaptive Threading Analysis Runner${NC}"
      echo -e "Usage: ./run_adaptive_threading_analysis.sh [options]"
      echo -e "\nOptions:"
      echo -e "  --verbose    Run benchmarks with verbose output"
      echo -e "  --help       Show this help message"
      echo -e "\nAnalysis Coverage:"
      echo -e "  - AI Threading Crossover: Find optimal entity count for multi-threading"
      echo -e "  - Collision Threading Crossover: Find optimal entity count for collision"
      echo -e "  - AI Adaptive Learning: 3000-frame throughput tracking test"
      echo -e "  - Collision Adaptive Learning: 3000-frame throughput tracking test"
      echo -e "  - Summary: Per-system throughput comparison"
      echo -e "\nEstimated Runtime: 5-8 minutes"
      exit 0
      ;;
  esac
done

# Create results directory
mkdir -p "$PROJECT_DIR/test_results"
RESULTS_FILE="$PROJECT_DIR/test_results/adaptive_threading_analysis_$(date +%Y%m%d_%H%M%S).txt"
CURRENT_FILE="$PROJECT_DIR/test_results/adaptive_threading_analysis_current.txt"

echo -e "${MAGENTA}======================================================${NC}"
echo -e "${MAGENTA}         Adaptive Threading Analysis                  ${NC}"
echo -e "${MAGENTA}======================================================${NC}"
echo -e "${YELLOW}Finding optimal threading thresholds per system${NC}"

# Check if the test executable exists
TEST_EXECUTABLE="$PROJECT_DIR/bin/debug/adaptive_threading_analysis"

if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo -e "${RED}Analysis executable not found: $TEST_EXECUTABLE${NC}"
    echo -e "${YELLOW}Make sure you have built the project with tests enabled.${NC}"
    echo -e "Run: ${CYAN}cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build${NC}"
    exit 1
fi

# Make sure the executable has proper permissions
chmod +x "$TEST_EXECUTABLE"

# Run the analysis
echo -e "${CYAN}Starting adaptive threading analysis...${NC}"
echo "Adaptive Threading Analysis - $(date)" > "$RESULTS_FILE"

if [ "$VERBOSE" = true ]; then
    echo -e "${YELLOW}Verbose mode enabled${NC}"
    $TEST_EXECUTABLE --log_level=all 2>&1 | tee -a "$RESULTS_FILE"
    RESULT=${PIPESTATUS[0]}
else
    $TEST_EXECUTABLE --log_level=test_suite 2>&1 | tee -a "$RESULTS_FILE"
    RESULT=${PIPESTATUS[0]}
fi

echo "" >> "$RESULTS_FILE"
echo "Analysis completed at: $(date)" >> "$RESULTS_FILE"
echo "Exit code: $RESULT" >> "$RESULTS_FILE"

# Copy to current file for regression comparison
cp "$RESULTS_FILE" "$CURRENT_FILE"

# Report results
echo -e "\n${MAGENTA}======================================================${NC}"
if [ $RESULT -eq 0 ]; then
    echo -e "${GREEN}All adaptive threading analyses completed successfully!${NC}"
    echo -e "\nAnalysis Coverage:"
    echo -e "  ${GREEN}✓${NC} AI Threading Crossover"
    echo -e "  ${GREEN}✓${NC} Collision Threading Crossover"
    echo -e "  ${GREEN}✓${NC} AI Adaptive Learning (3000 frames)"
    echo -e "  ${GREEN}✓${NC} Collision Adaptive Learning (3000 frames)"
    echo -e "  ${GREEN}✓${NC} Per-System Summary"
else
    echo -e "${RED}Some analyses failed or encountered issues${NC}"
    echo -e "${YELLOW}Check the detailed results in: $RESULTS_FILE${NC}"
fi

echo -e "\n${CYAN}Results saved to: $RESULTS_FILE${NC}"
echo -e "${CYAN}Current results: $CURRENT_FILE${NC}"
echo -e "${MAGENTA}======================================================${NC}"

# Key metrics
if [ $RESULT -eq 0 ]; then
    echo -e "\n${BLUE}Key Metrics Measured:${NC}"
    echo -e "  • Single-threaded throughput (items/ms)"
    echo -e "  • Multi-threaded throughput (items/ms)"
    echo -e "  • Speedup ratio (multi/single)"
    echo -e "  • Crossover point (where speedup > 1.1x)"
    echo -e "  • Batch multiplier adaptation"
fi

exit $RESULT
