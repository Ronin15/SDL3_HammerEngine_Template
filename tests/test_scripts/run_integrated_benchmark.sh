#!/bin/bash

# Copyright (c) 2025 Hammer Forged Games
# All rights reserved.
# Licensed under the MIT License - see LICENSE file for details

# Integrated System Load Benchmark Runner
# Tests all major managers under realistic combined load

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BIN_DIR="$PROJECT_ROOT/bin/debug"
BENCHMARK_EXECUTABLE="$BIN_DIR/integrated_system_benchmark"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Banner
echo -e "${BLUE}============================================${NC}"
echo -e "${BLUE}  Integrated System Load Benchmark${NC}"
echo -e "${BLUE}============================================${NC}"
echo ""

# Check if executable exists
if [ ! -f "$BENCHMARK_EXECUTABLE" ]; then
    echo -e "${RED}Error: Benchmark executable not found at:${NC}"
    echo "  $BENCHMARK_EXECUTABLE"
    echo ""
    echo -e "${YELLOW}Please build the tests first:${NC}"
    echo "  cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug"
    echo "  ninja -C build"
    exit 1
fi

# Parse command line arguments
VERBOSE=false
SPECIFIC_TEST=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --test|-t)
            SPECIFIC_TEST="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --verbose, -v          Show detailed output"
            echo "  --test <name>, -t      Run specific test case"
            echo "  --help, -h             Show this help message"
            echo ""
            echo "Available test cases:"
            echo "  TestRealisticGameSimulation60FPS"
            echo "  TestScalingUnderLoad"
            echo "  TestManagerCoordinationOverhead"
            echo "  TestSustainedPerformance"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Build command
CMD="$BENCHMARK_EXECUTABLE"

if [ -n "$SPECIFIC_TEST" ]; then
    echo -e "${BLUE}Running specific test: ${SPECIFIC_TEST}${NC}"
    CMD="$CMD --run_test=IntegratedSystemBenchmarkSuite/${SPECIFIC_TEST}"
else
    echo -e "${BLUE}Running all integrated system benchmarks${NC}"
    CMD="$CMD --run_test=IntegratedSystemBenchmarkSuite"
fi

if $VERBOSE; then
    CMD="$CMD --log_level=all"
else
    CMD="$CMD --log_level=test_suite"
fi

echo ""
echo -e "${YELLOW}Command: $CMD${NC}"
echo ""

# Run the benchmark
cd "$PROJECT_ROOT"

if $CMD; then
    echo ""
    echo -e "${GREEN}============================================${NC}"
    echo -e "${GREEN}  Benchmark completed successfully${NC}"
    echo -e "${GREEN}============================================${NC}"
    exit 0
else
    EXIT_CODE=$?
    echo ""
    echo -e "${RED}============================================${NC}"
    echo -e "${RED}  Benchmark failed with exit code $EXIT_CODE${NC}"
    echo -e "${RED}============================================${NC}"
    exit $EXIT_CODE
fi
