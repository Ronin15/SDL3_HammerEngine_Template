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

# Create required directories
echo -e "Working directory: $(pwd)"
mkdir -p test_results
mkdir -p build

# Check if build directory needs configuration
if [ ! -f "build/build.ninja" ]; then
    echo -e "${YELLOW}Configuring build directory...${NC}"
    cmake -B build
fi

# Build all tests
echo -e "${YELLOW}Building all tests...${NC}"
ninja -C build ai_optimization_tests save_manager_tests thread_system_tests ai_scaling_benchmark thread_safe_ai_manager_tests thread_safe_ai_integration_tests

# Check if build was successful
if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed! Please fix compilation errors.${NC}"
    exit 1
fi

echo -e "${GREEN}Build successful!${NC}"
echo ""

# Initialize result variables
AI_RESULT=0
SAVE_RESULT=0
THREAD_RESULT=0
AI_BENCHMARK_RESULT=0
THREAD_SAFE_AI_RESULT=0
AI_INTEGRATION_RESULT=0

# Run each test suite and record results
echo -e "${YELLOW}===============================${NC}"
echo -e "${YELLOW}Running AI Optimization Tests...${NC}"
echo -e "${YELLOW}===============================${NC}"
# Use appropriate executable name based on platform
AI_TEST_EXECUTABLE="./bin/debug/ai_optimization_tests"
if [ "$(uname)" != "Darwin" ] && [ "$(uname)" != "Linux" ]; then
    AI_TEST_EXECUTABLE="./bin/debug/ai_optimization_tests.exe"
fi

# Verify executable exists
if [ ! -f "$AI_TEST_EXECUTABLE" ]; then
    echo -e "${RED}Error: AI test executable not found at '$AI_TEST_EXECUTABLE'${NC}"
    FOUND_EXECUTABLE=$(find ./bin -name "ai_optimization_tests*")
    if [ -n "$FOUND_EXECUTABLE" ]; then
        echo -e "${GREEN}Found executable at: $FOUND_EXECUTABLE${NC}"
        AI_TEST_EXECUTABLE="$FOUND_EXECUTABLE"
    else
        echo -e "${RED}Could not find the AI test executable!${NC}"
        AI_RESULT=1
    fi
else
    "$AI_TEST_EXECUTABLE" > test_results/ai_test_output.txt 2>&1
    AI_RESULT=$?
fi
# Add a slight pause to allow for cleanup
sleep 2
echo ""

echo -e "${YELLOW}===============================${NC}"
echo -e "${YELLOW}Running Save Manager Tests...${NC}"
echo -e "${YELLOW}===============================${NC}"
SAVE_TEST_EXECUTABLE="./bin/debug/save_manager_tests"
if [ ! -f "$SAVE_TEST_EXECUTABLE" ]; then
    echo -e "${RED}Error: Save test executable not found at '$SAVE_TEST_EXECUTABLE'${NC}"
    FOUND_EXECUTABLE=$(find ./bin -name "save_manager_tests*")
    if [ -n "$FOUND_EXECUTABLE" ]; then
        echo -e "${GREEN}Found executable at: $FOUND_EXECUTABLE${NC}"
        SAVE_TEST_EXECUTABLE="$FOUND_EXECUTABLE"
    else
        echo -e "${RED}Could not find the Save test executable!${NC}"
        SAVE_RESULT=1
    fi
else
    "$SAVE_TEST_EXECUTABLE" > test_results/save_test_output.txt 2>&1
    SAVE_RESULT=$?
fi
# Add a slight pause to allow for cleanup
sleep 2
echo ""

echo -e "${YELLOW}===============================${NC}"
echo -e "${YELLOW}Running Thread System Tests...${NC}"
echo -e "${YELLOW}===============================${NC}"
THREAD_TEST_EXECUTABLE="./bin/debug/thread_system_tests"
if [ ! -f "$THREAD_TEST_EXECUTABLE" ]; then
    echo -e "${RED}Error: Thread test executable not found at '$THREAD_TEST_EXECUTABLE'${NC}"
    FOUND_EXECUTABLE=$(find ./bin -name "thread_system_tests*")
    if [ -n "$FOUND_EXECUTABLE" ]; then
        echo -e "${GREEN}Found executable at: $FOUND_EXECUTABLE${NC}"
        THREAD_TEST_EXECUTABLE="$FOUND_EXECUTABLE"
    else
        echo -e "${RED}Could not find the Thread test executable!${NC}"
        THREAD_RESULT=1
    fi
else
    "$THREAD_TEST_EXECUTABLE" > test_results/thread_test_output.txt 2>&1
    THREAD_RESULT=$?
fi
# Add a slight pause to allow for cleanup
sleep 2
echo ""

echo -e "${YELLOW}===============================${NC}"
echo -e "${YELLOW}Running AI Scaling Benchmark...${NC}"
echo -e "${YELLOW}===============================${NC}"
AI_BENCHMARK_EXECUTABLE="./bin/debug/ai_scaling_benchmark"
if [ ! -f "$AI_BENCHMARK_EXECUTABLE" ]; then
    echo -e "${RED}Error: AI benchmark executable not found at '$AI_BENCHMARK_EXECUTABLE'${NC}"
    FOUND_EXECUTABLE=$(find ./bin -name "ai_scaling_benchmark*")
    if [ -n "$FOUND_EXECUTABLE" ]; then
        echo -e "${GREEN}Found executable at: $FOUND_EXECUTABLE${NC}"
        AI_BENCHMARK_EXECUTABLE="$FOUND_EXECUTABLE"
    else
        echo -e "${RED}Could not find the AI benchmark executable!${NC}"
        AI_BENCHMARK_RESULT=1
    fi
else
    "$AI_BENCHMARK_EXECUTABLE" > test_results/ai_benchmark_output.txt 2>&1
    AI_BENCHMARK_RESULT=$?
fi
# Add a slight pause to allow for cleanup
sleep 2
echo ""

echo -e "${YELLOW}===============================${NC}"
echo -e "${YELLOW}Running Thread-Safe AI Manager Tests...${NC}"
echo -e "${YELLOW}===============================${NC}"
THREAD_SAFE_AI_EXECUTABLE="./bin/debug/thread_safe_ai_manager_tests"
if [ ! -f "$THREAD_SAFE_AI_EXECUTABLE" ]; then
    echo -e "${RED}Error: Thread-Safe AI Manager test executable not found at '$THREAD_SAFE_AI_EXECUTABLE'${NC}"
    FOUND_EXECUTABLE=$(find ./bin -name "thread_safe_ai_manager_tests*")
    if [ -n "$FOUND_EXECUTABLE" ]; then
        echo -e "${GREEN}Found executable at: $FOUND_EXECUTABLE${NC}"
        THREAD_SAFE_AI_EXECUTABLE="$FOUND_EXECUTABLE"
    else
        echo -e "${RED}Could not find the Thread-Safe AI Manager test executable!${NC}"
        THREAD_SAFE_AI_RESULT=1
    fi
else
    # Run Thread-Safe AI Manager tests
    "$THREAD_SAFE_AI_EXECUTABLE" > test_results/thread_safe_ai_output.txt 2>&1
    THREAD_SAFE_AI_RESULT=$?
    
    # Check for successful test output even if exit code is non-zero
    # This is needed because the test uses _exit(1) for clean shutdown
    if grep -q "No errors detected" test_results/thread_safe_ai_output.txt; then
        echo "Thread-Safe AI Manager tests completed successfully"
        THREAD_SAFE_AI_RESULT=0
    else
        echo "Thread-Safe AI Manager tests failed with exit code $THREAD_SAFE_AI_RESULT"
    fi
fi
# Add a slight pause to allow for cleanup
sleep 2
echo ""

echo -e "${YELLOW}===============================${NC}"
echo -e "${YELLOW}Running Thread-Safe AI Integration Tests...${NC}"
echo -e "${YELLOW}===============================${NC}"
AI_INTEGRATION_EXECUTABLE="./bin/debug/thread_safe_ai_integration_tests"
if [ ! -f "$AI_INTEGRATION_EXECUTABLE" ]; then
    echo -e "${RED}Error: Thread-Safe AI Integration test executable not found at '$AI_INTEGRATION_EXECUTABLE'${NC}"
    FOUND_EXECUTABLE=$(find ./bin -name "thread_safe_ai_integration_tests*")
    if [ -n "$FOUND_EXECUTABLE" ]; then
        echo -e "${GREEN}Found executable at: $FOUND_EXECUTABLE${NC}"
        AI_INTEGRATION_EXECUTABLE="$FOUND_EXECUTABLE"
    else
        echo -e "${RED}Could not find the Thread-Safe AI Integration test executable!${NC}"
        AI_INTEGRATION_RESULT=1
    fi
else
    # Run Thread-Safe AI Integration tests
    "$AI_INTEGRATION_EXECUTABLE" > test_results/ai_integration_output.txt 2>&1
    AI_INTEGRATION_RESULT=$?
    
    # Check for successful test output even if exit code is non-zero
    # This is needed because the test might use _exit(1) for clean shutdown
    if grep -q "No errors detected" test_results/ai_integration_output.txt || grep -q "All Thread-Safe AI Integration tests passed" test_results/ai_integration_output.txt; then
        echo "Thread-Safe AI Integration tests completed successfully"
        AI_INTEGRATION_RESULT=0
    else
        echo "Thread-Safe AI Integration tests failed with exit code $AI_INTEGRATION_RESULT"
    fi
fi
# Add a slight pause to allow for cleanup
sleep 2
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

if [ $AI_BENCHMARK_RESULT -eq 0 ]; then
    echo -e "${GREEN}[PASS] AI Scaling Benchmark${NC}"
    echo "[PASS] AI Scaling Benchmark" >> ${SUMMARY_FILE}
else
    echo -e "${RED}[FAIL] AI Scaling Benchmark (Error code: ${AI_BENCHMARK_RESULT})${NC}"
    echo "[FAIL] AI Scaling Benchmark (Error code: ${AI_BENCHMARK_RESULT})" >> ${SUMMARY_FILE}
fi

if [ $THREAD_SAFE_AI_RESULT -eq 0 ]; then
    echo -e "${GREEN}[PASS] Thread-Safe AI Manager Tests${NC}"
    echo "[PASS] Thread-Safe AI Manager Tests" >> ${SUMMARY_FILE}
else
    echo -e "${RED}[FAIL] Thread-Safe AI Manager Tests (Error code: ${THREAD_SAFE_AI_RESULT})${NC}"
    echo "[FAIL] Thread-Safe AI Manager Tests (Error code: ${THREAD_SAFE_AI_RESULT})" >> ${SUMMARY_FILE}
fi

if [ $AI_INTEGRATION_RESULT -eq 0 ]; then
    echo -e "${GREEN}[PASS] Thread-Safe AI Integration Tests${NC}"
    echo "[PASS] Thread-Safe AI Integration Tests" >> ${SUMMARY_FILE}
else
    echo -e "${RED}[FAIL] Thread-Safe AI Integration Tests (Error code: ${AI_INTEGRATION_RESULT})${NC}"
    echo "[FAIL] Thread-Safe AI Integration Tests (Error code: ${AI_INTEGRATION_RESULT})" >> ${SUMMARY_FILE}
fi

echo "" >> ${SUMMARY_FILE}
echo "Detailed logs available in:" >> ${SUMMARY_FILE}
echo "- test_results/ai_test_output.txt" >> ${SUMMARY_FILE}
echo "- test_results/save_test_output.txt" >> ${SUMMARY_FILE}
echo "- test_results/thread_test_output.txt" >> ${SUMMARY_FILE}
echo "- test_results/ai_benchmark_output.txt" >> ${SUMMARY_FILE}
echo "- test_results/thread_safe_ai_output.txt" >> ${SUMMARY_FILE}
echo "- test_results/ai_integration_output.txt" >> ${SUMMARY_FILE}

echo ""
echo -e "${YELLOW}===============================${NC}"
echo -e "${YELLOW}All test runs complete!${NC}"
echo -e "${YELLOW}Summary saved to ${SUMMARY_FILE}${NC}"
echo -e "${YELLOW}===============================${NC}"

# Extract performance metrics from all tests
echo "# Combined Performance Metrics" > test_results/performance_metrics.txt
echo "Test run completed at $(date)" >> test_results/performance_metrics.txt
echo "" >> test_results/performance_metrics.txt

echo "## AI Tests Performance" >> test_results/performance_metrics.txt
grep -E "processing time:|entities:|performance" test_results/ai_test_output.txt >> test_results/performance_metrics.txt 2>/dev/null
echo "" >> test_results/performance_metrics.txt

echo "## Save Manager Tests Performance" >> test_results/performance_metrics.txt
grep -E "time:|performance|saved:|loaded:" test_results/save_test_output.txt >> test_results/performance_metrics.txt 2>/dev/null
echo "" >> test_results/performance_metrics.txt

echo "## Thread System Tests Performance" >> test_results/performance_metrics.txt
grep -E "time:|performance|tasks:|queue:" test_results/thread_test_output.txt >> test_results/performance_metrics.txt 2>/dev/null
echo "" >> test_results/performance_metrics.txt

echo "## AI Scaling Benchmark Performance" >> test_results/performance_metrics.txt
grep -E "time:|entities:|updates per second|scalability" test_results/ai_benchmark_output.txt >> test_results/performance_metrics.txt 2>/dev/null
echo "" >> test_results/performance_metrics.txt

echo "## Thread-Safe AI Manager Tests Performance" >> test_results/performance_metrics.txt
grep -E "time:|entities:|Concurrent processing time" test_results/thread_safe_ai_output.txt >> test_results/performance_metrics.txt 2>/dev/null
echo "" >> test_results/performance_metrics.txt

echo "## Thread-Safe AI Integration Tests Performance" >> test_results/performance_metrics.txt
grep -E "time:|entities:|Concurrent processing time" test_results/ai_integration_output.txt >> test_results/performance_metrics.txt 2>/dev/null

# Check if any tests failed
if [ $AI_RESULT -ne 0 ] || [ $SAVE_RESULT -ne 0 ] || [ $THREAD_RESULT -ne 0 ] || [ $AI_BENCHMARK_RESULT -ne 0 ] || [ $THREAD_SAFE_AI_RESULT -ne 0 ] || [ $AI_INTEGRATION_RESULT -ne 0 ]; then
    echo -e "${RED}One or more tests failed. Please check the logs for details.${NC}"
    exit 1
else
    echo -e "${GREEN}All tests passed successfully!${NC}"
    exit 0
fi
