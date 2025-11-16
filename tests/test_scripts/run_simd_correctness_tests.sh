#!/bin/bash
# Script to run the SIMD Correctness Tests
# Copyright (c) 2025 Hammer Forged Games, MIT License

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${YELLOW}Running SIMD Correctness Tests...${NC}"

BUILD_TYPE="Debug"
VERBOSE=false

while [[ $# -gt 0 ]]; do
  case $1 in
    --debug) BUILD_TYPE="Debug"; shift ;;
    --release) BUILD_TYPE="Release"; shift ;;
    --verbose) VERBOSE=true; shift ;;
    --help)
      echo "Usage: $0 [--debug] [--release] [--verbose] [--help]"
      exit 0
      ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [ "$BUILD_TYPE" = "Debug" ]; then
  TEST_EXECUTABLE="$PROJECT_ROOT/bin/debug/simd_correctness_tests"
else
  TEST_EXECUTABLE="$PROJECT_ROOT/bin/release/simd_correctness_tests"
fi

if [ ! -f "$TEST_EXECUTABLE" ]; then
  echo -e "${RED}Error: Test executable not found at '$TEST_EXECUTABLE'${NC}"
  exit 1
fi

mkdir -p "$PROJECT_ROOT/test_results"
OUTPUT_FILE="$PROJECT_ROOT/test_results/simd_correctness_tests_output.txt"

TEST_OPTS="--log_level=all --catch_system_errors=no"
if [ "$VERBOSE" = true ]; then
  TEST_OPTS="$TEST_OPTS --report_level=detailed"
fi

"$TEST_EXECUTABLE" $TEST_OPTS 2>&1 | tee "$OUTPUT_FILE"
TEST_RESULT=${PIPESTATUS[0]}

if [ $TEST_RESULT -ne 0 ] || grep -q "failure\|test cases failed\|fatal error" "$OUTPUT_FILE"; then
  echo -e "${RED}❌ Some tests failed! See $OUTPUT_FILE for details.${NC}"
  exit 1
else
  echo -e "${GREEN}✅ All SIMD Correctness tests passed!${NC}"
  exit 0
fi
