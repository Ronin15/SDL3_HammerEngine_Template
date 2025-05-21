#!/bin/bash

# run_all_tests.sh - Master script to run all test suites
# Copyright (c) 2025 Hammer Forged Games

# Print header
echo -e "\e[1;33mForge Engine Testing Suite\e[0m"
echo -e "\e[1;33m=========================\e[0m"
echo ""

# Set colors for better readability
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Navigate to project root directory (in case script is run from elsewhere)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Create results directory if it doesn't exist
echo -e "Working directory: $(pwd)"
if [ ! -d "test_results" ]; then
    echo -e "Creating results directory: $(pwd)/test_results"
    mkdir -p test_results
fi

# Create build directory if it doesn't exist
if [ ! -d "build" ]; then
    echo -e "${YELLOW}Creating build directory...${NC}"
    mkdir -p build
    cmake -B build
fi

# Build all tests
echo -e "${YELLOW}Building all tests...${NC}"
ninja -C build ai_optimization_tests save_manager_tests thread_system_tests

# Check if build was successful
if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed! Please fix compilation errors.${NC}"
    exit 1
fi

echo -e "${GREEN}Build successful!${NC}"
echo ""

# Run each test suite and record results
echo -e "${YELLOW}===============================${NC}"
echo -e "${YELLOW}Running AI Optimization Tests...${NC}"
echo -e "${YELLOW}===============================${NC}"
# Use appropriate executable name based on platform
if [ "$(uname)" == "Darwin" ] || [ "$(uname)" == "Linux" ]; then
    ./bin/debug/ai_optimization_tests > test_results/ai_test_output.txt 2>&1
else
    ./bin/debug/ai_optimization_tests.exe > test_results/ai_test_output.txt 2>&1
fi
AI_RESULT=$?
echo ""

echo -e "${YELLOW}===============================${NC}"
echo -e "${YELLOW}Running Save Manager Tests...${NC}"
echo -e "${YELLOW}===============================${NC}"
./bin/debug/save_manager_tests > test_results/save_test_output.txt 2>&1
SAVE_RESULT=$?
echo ""

echo -e "${YELLOW}===============================${NC}"
echo -e "${YELLOW}Running Thread System Tests...${NC}"
echo -e "${YELLOW}===============================${NC}"
./bin/debug/thread_system_tests > test_results/thread_test_output.txt 2>&1
THREAD_RESULT=$?
echo ""

echo -e "${YELLOW}===============================${NC}"
echo -e "${YELLOW}TEST RESULTS SUMMARY${NC}"
echo -e "${YELLOW}===============================${NC}"

# Generate timestamp for the report
TIMESTAMP=$(date +"%Y-%m-%d_%H-%M-%S")
SUMMARY_FILE="test_results/summary_report_${TIMESTAMP}.txt"

# Create a summary report
echo "Test Results Summary" > ${SUMMARY_FILE}
echo "===================" >> ${SUMMARY_FILE}
echo "Generated: $(date)" >> ${SUMMARY_FILE}
echo "" >> ${SUMMARY_FILE}

if [ $AI_RESULT -eq 0 ]; then
    echo -e "${GREEN}[PASS] AI Optimization Tests${NC}"
    echo "[PASS] AI Optimization Tests" >> ${SUMMARY_FILE}
else
    echo -e "${RED}[FAIL] AI Optimization Tests (Error code: ${AI_RESULT})${NC}"
    echo "[FAIL] AI Optimization Tests (Error code: ${AI_RESULT})" >> ${SUMMARY_FILE}
fi

if [ $SAVE_RESULT -eq 0 ]; then
    echo -e "${GREEN}[PASS] Save Manager Tests${NC}"
    echo "[PASS] Save Manager Tests" >> ${SUMMARY_FILE}
else
    echo -e "${RED}[FAIL] Save Manager Tests (Error code: ${SAVE_RESULT})${NC}"
    echo "[FAIL] Save Manager Tests (Error code: ${SAVE_RESULT})" >> ${SUMMARY_FILE}
fi

if [ $THREAD_RESULT -eq 0 ]; then
    echo -e "${GREEN}[PASS] Thread System Tests${NC}"
    echo "[PASS] Thread System Tests" >> ${SUMMARY_FILE}
else
    echo -e "${RED}[FAIL] Thread System Tests (Error code: ${THREAD_RESULT})${NC}"
    echo "[FAIL] Thread System Tests (Error code: ${THREAD_RESULT})" >> ${SUMMARY_FILE}
fi

echo "" >> ${SUMMARY_FILE}
echo "Detailed logs available in:" >> ${SUMMARY_FILE}
echo "- test_results/ai_test_output.txt" >> ${SUMMARY_FILE}
echo "- test_results/save_test_output.txt" >> ${SUMMARY_FILE}
echo "- test_results/thread_test_output.txt" >> ${SUMMARY_FILE}

echo ""
echo -e "${YELLOW}===============================${NC}"
echo -e "${YELLOW}All test runs complete!${NC}"
echo -e "${YELLOW}Summary saved to ${SUMMARY_FILE}${NC}"
echo -e "${YELLOW}===============================${NC}"

# Extract performance metrics (for AI tests)
grep "processing time:" test_results/ai_test_output.txt > test_results/performance_metrics.txt 2>/dev/null
if [ $? -ne 0 ]; then
    # Try alternative format
    grep "processing time" test_results/ai_test_output.txt > test_results/performance_metrics.txt
fi
echo "Test completed at $(date)" >> test_results/performance_metrics.txt

# Check if any tests failed
if [ $AI_RESULT -ne 0 ] || [ $SAVE_RESULT -ne 0 ] || [ $THREAD_RESULT -ne 0 ]; then
    echo -e "${RED}One or more tests failed. Please check the logs for details.${NC}"
    exit 1
else
    echo -e "${GREEN}All tests passed successfully!${NC}"
    exit 0
fi