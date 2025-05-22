#!/bin/bash

# run_ai_tests.sh - Script to build and run all AI tests
# Copyright (c) 2025 Hammer Forged Games

# Set colors for better readability
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${YELLOW}Building All AI Tests...${NC}"

# Navigate to project root directory (in case script is run from elsewhere)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Create build directory if it doesn't exist
if [ ! -d "build" ]; then
    echo -e "${YELLOW}Creating build directory...${NC}"
    mkdir -p build
    cmake -B build
fi

# Build all AI tests
echo -e "${YELLOW}Compiling tests...${NC}"
ninja -C build ai_optimization_tests ai_scaling_benchmark thread_safe_ai_manager_tests thread_safe_ai_integration_tests

# Check if build was successful
if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed! Please fix compilation errors.${NC}"
    exit 1
fi

echo -e "${GREEN}Build successful!${NC}"

# Create test_results directory if it doesn't exist
mkdir -p test_results

# Run the tests
mkdir -p test_results

# Function to run a test and capture output
run_test() {
    local test_name=$1
    local executable_name=$2
    
    echo -e "${YELLOW}Running $test_name...${NC}"
    
    # Use the appropriate extension based on OS
    if [ "$(uname)" == "Darwin" ] || [ "$(uname)" == "Linux" ]; then
        TEST_EXECUTABLE="./bin/debug/$executable_name"
    else
        TEST_EXECUTABLE="./bin/debug/${executable_name}.exe"
    fi

    # Verify executable exists
    if [ ! -f "$TEST_EXECUTABLE" ]; then
        echo -e "${RED}Error: Test executable not found at '$TEST_EXECUTABLE'${NC}"
        # Attempt to find the executable
        echo -e "${YELLOW}Searching for test executable...${NC}"
        FOUND_EXECUTABLE=$(find ./bin -name "$executable_name*")
        if [ -n "$FOUND_EXECUTABLE" ]; then
            echo -e "${GREEN}Found executable at: $FOUND_EXECUTABLE${NC}"
            TEST_EXECUTABLE="$FOUND_EXECUTABLE"
        else
            echo -e "${RED}Could not find the test executable. Build may have failed.${NC}"
            return 1
        fi
    fi

    # Create a temporary file for test output
    TEMP_OUTPUT=$(mktemp)

    # Run the tests and capture output
    "$TEST_EXECUTABLE" | tee "$TEMP_OUTPUT"
    local result=$?

    # Save test results
    local output_file="test_results/${executable_name}_output.txt"
    cp "$TEMP_OUTPUT" "$output_file"

    # Extract performance metrics
    echo -e "${YELLOW}Extracting performance metrics...${NC}"
    grep -E "time:|entities:|processed:|performance|Concurrent processing time" "$TEMP_OUTPUT" > "test_results/${executable_name}_performance_metrics.txt"

    # Clean up temporary file
    rm "$TEMP_OUTPUT"

    # Check if tests were successful
    if [ $result -ne 0 ]; then
        echo -e "${RED}$test_name failed!${NC}"
        return 1
    else
        echo -e "${GREEN}$test_name passed successfully!${NC}"
        return 0
    fi
}

# Run all AI tests
echo -e "${YELLOW}===============================${NC}"
run_test "AI Optimization Tests" "ai_optimization_tests"
AI_OPTIMIZATION_RESULT=$?
echo -e "${YELLOW}===============================${NC}"
# Add a slight pause to allow for cleanup
sleep 2

echo -e "${YELLOW}===============================${NC}"
run_test "AI Scaling Benchmark" "ai_scaling_benchmark"
AI_BENCHMARK_RESULT=$?
echo -e "${YELLOW}===============================${NC}"
# Add a slight pause to allow for cleanup
sleep 2

echo -e "${YELLOW}===============================${NC}"
run_test "Thread-Safe AI Manager Tests" "thread_safe_ai_manager_tests"
THREAD_SAFE_AI_RESULT=$?
echo -e "${YELLOW}===============================${NC}"
# Add a slight pause to allow for cleanup
sleep 2

echo -e "${YELLOW}===============================${NC}"
run_test "Thread-Safe AI Integration Tests" "thread_safe_ai_integration_tests"
AI_INTEGRATION_RESULT=$?
echo -e "${YELLOW}===============================${NC}"
# Add a slight pause to allow for cleanup
sleep 2

# Check overall result
if [ $AI_OPTIMIZATION_RESULT -ne 0 ] || [ $AI_BENCHMARK_RESULT -ne 0 ] || [ $THREAD_SAFE_AI_RESULT -ne 0 ] || [ $AI_INTEGRATION_RESULT -ne 0 ]; then
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
else
    echo -e "${GREEN}All tests passed successfully!${NC}"
fi

echo -e "${YELLOW}Performance Summary:${NC}"
echo "1. Entity Component Caching: Reduces map lookups for faster entity-behavior access"
echo "2. Batch Processing: Groups similar entities for better cache coherency"
echo "3. Early Exit Conditions: Skips unnecessary updates based on custom conditions"
echo "4. Message Queue System: Batches messages for efficient delivery"
echo "5. Thread-Safe Operations: Concurrent AI behavior processing"
echo "6. AI Scaling: Performance with increasing entity counts"
echo "7. Thread Integration: Thread system and AI system working together"

# Create summary report
TIMESTAMP=$(date +"%Y-%m-%d_%H-%M-%S")
SUMMARY_FILE="test_results/ai_tests_summary_${TIMESTAMP}.txt"

echo "AI Tests Summary" > ${SUMMARY_FILE}
echo "==============" >> ${SUMMARY_FILE}
echo "Generated: $(date)" >> ${SUMMARY_FILE}
echo "" >> ${SUMMARY_FILE}

if [ $AI_OPTIMIZATION_RESULT -eq 0 ]; then
    echo "[PASS] AI Optimization Tests" >> ${SUMMARY_FILE}
else
    echo "[FAIL] AI Optimization Tests" >> ${SUMMARY_FILE}
fi

if [ $AI_BENCHMARK_RESULT -eq 0 ]; then
    echo "[PASS] AI Scaling Benchmark" >> ${SUMMARY_FILE}
else
    echo "[FAIL] AI Scaling Benchmark" >> ${SUMMARY_FILE}
fi

if [ $THREAD_SAFE_AI_RESULT -eq 0 ]; then
    echo "[PASS] Thread-Safe AI Manager Tests" >> ${SUMMARY_FILE}
else
    echo "[FAIL] Thread-Safe AI Manager Tests" >> ${SUMMARY_FILE}
fi

if [ $AI_INTEGRATION_RESULT -eq 0 ]; then
    echo "[PASS] Thread-Safe AI Integration Tests" >> ${SUMMARY_FILE}
else
    echo "[FAIL] Thread-Safe AI Integration Tests" >> ${SUMMARY_FILE}
fi

echo -e "${YELLOW}Summary saved to ${SUMMARY_FILE}${NC}"

exit 0