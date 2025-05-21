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

# Run the tests
echo -e "${YELLOW}Running AI Optimization Tests...${NC}"
# Use the appropriate extension based on OS
if [ "$(uname)" == "Darwin" ] || [ "$(uname)" == "Linux" ]; then
    ./bin/debug/ai_optimization_tests
else
    ./bin/debug/ai_optimization_tests.exe
fi

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