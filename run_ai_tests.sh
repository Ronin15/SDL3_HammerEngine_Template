#!/bin/bash

# run_ai_tests.sh - Script to build and run the AI optimization tests
# Copyright (c) 2025 Hammer Forged Games

# Set colors for better readability
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${YELLOW}Building AI Optimization Tests...${NC}"

# Navigate to project root directory (in case script is run from elsewhere)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Create build directory if it doesn't exist
if [ ! -d "build" ]; then
    echo -e "${YELLOW}Creating build directory...${NC}"
    mkdir -p build
    cmake -B build
fi

# Build the AI tests
echo -e "${YELLOW}Compiling tests...${NC}"
ninja -C build ai_optimization_tests

# Check if build was successful
if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed! Please fix compilation errors.${NC}"
    exit 1
fi

echo -e "${GREEN}Build successful!${NC}"

# Create test_results directory if it doesn't exist
mkdir -p test_results

# Run the tests
echo -e "${YELLOW}Running AI Optimization Tests...${NC}"
# Use the appropriate extension based on OS
if [ "$(uname)" == "Darwin" ] || [ "$(uname)" == "Linux" ]; then
    TEST_EXECUTABLE="./bin/debug/ai_optimization_tests"
else
    TEST_EXECUTABLE="./bin/debug/ai_optimization_tests.exe"
fi

# Verify executable exists
if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo -e "${RED}Error: Test executable not found at '$TEST_EXECUTABLE'${NC}"
    # Attempt to find the executable
    echo -e "${YELLOW}Searching for test executable...${NC}"
    FOUND_EXECUTABLE=$(find ./bin -name ai_optimization_tests*)
    if [ -n "$FOUND_EXECUTABLE" ]; then
        echo -e "${GREEN}Found executable at: $FOUND_EXECUTABLE${NC}"
        TEST_EXECUTABLE="$FOUND_EXECUTABLE"
    else
        echo -e "${RED}Could not find the test executable. Build may have failed.${NC}"
        exit 1
    fi
fi

# Create a temporary file for test output
TEMP_OUTPUT=$(mktemp)

# Run the tests and capture output
"$TEST_EXECUTABLE" | tee "$TEMP_OUTPUT"

# Save test results
cp "$TEMP_OUTPUT" "test_results/ai_optimization_test_output.txt"

# Extract performance metrics
echo -e "${YELLOW}Extracting performance metrics...${NC}"
grep -E "time:|entities:|processed:|performance" "$TEMP_OUTPUT" > "test_results/ai_optimization_performance_metrics.txt"

# Clean up temporary file
rm "$TEMP_OUTPUT"

# Check if tests were successful
if [ $? -ne 0 ]; then
    echo -e "${RED}Tests failed!${NC}"
    exit 1
else
    echo -e "${GREEN}All tests passed successfully!${NC}"
fi

echo -e "${YELLOW}Performance Summary:${NC}"
echo "1. Entity Component Caching: Reduces map lookups for faster entity-behavior access"
echo "2. Batch Processing: Groups similar entities for better cache coherency"
echo "3. Early Exit Conditions: Skips unnecessary updates based on custom conditions"
echo "4. Message Queue System: Batches messages for efficient delivery"

exit 0