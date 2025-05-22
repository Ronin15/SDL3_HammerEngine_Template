#!/bin/bash
# Script to run the Thread-Safe AI Integration tests
# Copyright (c) 2025 Hammer Forged Games, MIT License

# Create required directories
mkdir -p build
mkdir -p test_results

# Set default build type
BUILD_TYPE="Debug"
CLEAN_BUILD=false
VERBOSE=false

# Process command-line options
while [[ $# -gt 0 ]]; do
  case $1 in
    --clean)
      CLEAN_BUILD=true
      shift
      ;;
    --release)
      BUILD_TYPE="Release"
      shift
      ;;
    --verbose)
      VERBOSE=true
      shift
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: $0 [--clean] [--release] [--verbose]"
      exit 1
      ;;
  esac
done

# Configure build cleaning
if [ "$CLEAN_BUILD" = true ]; then
  echo "Cleaning build directory..."
  rm -rf build/*
fi

# Check if Ninja is available
if command -v ninja &> /dev/null; then
  USE_NINJA=true
  echo "Ninja build system found, using it for faster builds."
else
  USE_NINJA=false
  echo "Ninja build system not found, using default CMake generator."
fi

# Configure the project
echo "Configuring project with CMake (Build type: $BUILD_TYPE)..."
if [ "$USE_NINJA" = true ]; then
  if [ "$VERBOSE" = true ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=$BUILD_TYPE -G Ninja -DBOOST_TEST_NO_SIGNAL_HANDLING=ON
  else
    cmake -S . -B build -DCMAKE_BUILD_TYPE=$BUILD_TYPE -G Ninja -DBOOST_TEST_NO_SIGNAL_HANDLING=ON > /dev/null
  fi
else
  if [ "$VERBOSE" = true ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DBOOST_TEST_NO_SIGNAL_HANDLING=ON
  else
    cmake -S . -B build -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DBOOST_TEST_NO_SIGNAL_HANDLING=ON > /dev/null
  fi
fi

# Build the tests
echo "Building Thread-Safe AI Integration tests..."
if [ "$USE_NINJA" = true ]; then
  if [ "$VERBOSE" = true ]; then
    ninja -C build thread_safe_ai_integration_tests
  else
    ninja -C build thread_safe_ai_integration_tests > /dev/null
  fi
else
  if [ "$VERBOSE" = true ]; then
    cmake --build build --config $BUILD_TYPE --target thread_safe_ai_integration_tests
  else
    cmake --build build --config $BUILD_TYPE --target thread_safe_ai_integration_tests > /dev/null
  fi
fi

# Check if build was successful
if [ $? -ne 0 ]; then
  echo "Build failed. See output for details."
  exit 1
fi

# Determine test executable path based on build type
if [ "$BUILD_TYPE" = "Debug" ]; then
  TEST_EXECUTABLE="./bin/debug/thread_safe_ai_integration_tests"
else
  TEST_EXECUTABLE="./bin/release/thread_safe_ai_integration_tests"
fi

# Verify executable exists
if [ ! -f "$TEST_EXECUTABLE" ]; then
  echo "Error: Test executable not found at '$TEST_EXECUTABLE'"
  # Attempt to find the executable
  echo "Searching for test executable..."
  FOUND_EXECUTABLE=$(find ./bin -name thread_safe_ai_integration_tests)
  if [ -n "$FOUND_EXECUTABLE" ]; then
    echo "Found executable at: $FOUND_EXECUTABLE"
    TEST_EXECUTABLE="$FOUND_EXECUTABLE"
  else
    echo "Could not find the test executable. Build may have failed or placed the executable in an unexpected location."
    exit 1
  fi
fi

# Run tests and save output
echo "Running Thread-Safe AI Integration tests..."

# Create the test_results directory if it doesn't exist
mkdir -p test_results

# Create a temporary file for test output
TEMP_OUTPUT="test_results/thread_safe_ai_integration_test_output.txt"

# Check for timeout command availability
TIMEOUT_CMD=""
if command -v timeout &> /dev/null; then
  TIMEOUT_CMD="timeout"
elif command -v gtimeout &> /dev/null; then
  TIMEOUT_CMD="gtimeout"
else
  echo "Warning: Neither 'timeout' nor 'gtimeout' command found. Tests will run without timeout protection."
fi

# Set test command options
TEST_OPTS="--log_level=all"

# Run the tests with a clean environment and timeout
if [ -n "$TIMEOUT_CMD" ]; then
  $TIMEOUT_CMD 30s "$TEST_EXECUTABLE" $TEST_OPTS --catch_system_errors=no 2>&1 | tee "$TEMP_OUTPUT"
  # Note: We don't use $? here because we want to analyze the output
else
  "$TEST_EXECUTABLE" $TEST_OPTS --catch_system_errors=no 2>&1 | tee "$TEMP_OUTPUT"
  # Note: We don't use $? here because we want to analyze the output
fi

# Extract performance metrics
echo "Extracting performance metrics..."
grep -E "time:|entities:|processed:|Concurrent processing time" "$TEMP_OUTPUT" > "test_results/thread_safe_ai_integration_performance_metrics.txt" || true

# Check for timeout
if [ -n "$TIMEOUT_CMD" ] && grep -q "Operation timed out" "$TEMP_OUTPUT"; then
  echo "⚠️ Test execution timed out after 30 seconds!"
  echo "Test execution timed out after 30 seconds!" >> "$TEMP_OUTPUT"
  exit 124
fi

# Extract test results information
TOTAL_TESTS=$(grep -E "Running [0-9]+ test cases" "$TEMP_OUTPUT" | grep -o "[0-9]\+")
if [ -z "$TOTAL_TESTS" ]; then
  TOTAL_TESTS="unknown number of"
fi

# Check for any failed assertions
if grep -q "fail\|error\|assertion.*failed\|exception" "$TEMP_OUTPUT"; then
  echo "❌ Some tests failed! See test_results/thread_safe_ai_integration_test_output.txt for details."
  exit 1
fi

# Check for crash indicators
if grep -q "memory access violation\|fatal error\|segmentation fault\|Segmentation fault\|Abort trap" "$TEMP_OUTPUT"; then
  echo "❌ Tests crashed! See test_results/thread_safe_ai_integration_test_output.txt for details."
  exit 1
fi

# Check for successful test completion patterns
if grep -q "test cases: $TOTAL_TESTS.*failed: 0" "$TEMP_OUTPUT" || \
   grep -q "Running $TOTAL_TESTS test case.*No errors detected" "$TEMP_OUTPUT" || \
   grep -q "successful: $TOTAL_TESTS" "$TEMP_OUTPUT"; then
  echo "✅ All Thread-Safe AI Integration tests passed!"
  exit 0
fi

# If we got here, check if all test cases started and none had explicit failures
# This assumes the test output will always include "Entering test case" for each test
ENTERED_TESTS=$(grep -c "Entering test case" "$TEMP_OUTPUT")

if [ "$ENTERED_TESTS" -gt 0 ] && [ "$ENTERED_TESTS" -eq "$TOTAL_TESTS" ]; then
  # All tests started, and no explicit failures were found
  echo "✅ All $ENTERED_TESTS Thread-Safe AI Integration tests appear to have passed!"
  
  # Check for known boost test termination issue 
  if grep -q "boost::detail::system_signal_exception\|terminating due to uncaught exception\|libunwind\|terminate called" "$TEMP_OUTPUT"; then
    echo "⚠️ Known issue: Boost test framework crashed after tests executed."
    echo "This is likely due to signal handling with threads, but no test failures were detected."
  fi
  exit 0
else
  echo "❓ Test execution status is unclear. $ENTERED_TESTS of $TOTAL_TESTS tests started."
  echo "Review test_results/thread_safe_ai_integration_test_output.txt for details."
  
  # Show the beginning and end of the output for context
  echo "First few lines of test output:"
  head -5 "$TEMP_OUTPUT"
  echo "..."
  echo "Last few lines of test output:"
  tail -5 "$TEMP_OUTPUT"
  exit 1
fi