#!/bin/bash
# Script to run the AI Scaling Benchmark
# Copyright (c) 2025 Hammer Forged Games, MIT License

# Set colors for better readability
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${YELLOW}Running AI Scaling Benchmark...${NC}"

# Navigate to project root directory (in case script is run from elsewhere)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Create build directory if it doesn't exist
mkdir -p build
mkdir -p test_results

# Set default build type
BUILD_TYPE="Debug"
CLEAN_BUILD=false
VERBOSE=false
EXTREME_TEST=false

# Process command-line options
while [[ $# -gt 0 ]]; do
  case $1 in
    --clean)
      CLEAN_BUILD=true
      shift
      ;;
    --debug)
      BUILD_TYPE="Debug"
      shift
      ;;
    --verbose)
      VERBOSE=true
      shift
      ;;
    --extreme)
      EXTREME_TEST=true
      shift
      ;;
    --release)
      BUILD_TYPE="Release"
      shift
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: $0 [--clean] [--debug] [--verbose] [--extreme] [--release]"
      exit 1
      ;;
  esac
done

# Configure build cleaning
if [ "$CLEAN_BUILD" = true ]; then
  echo -e "${YELLOW}Cleaning build directory...${NC}"
  rm -rf build/*
fi

# Check if Ninja is available
if command -v ninja &> /dev/null; then
  USE_NINJA=true
  echo -e "${GREEN}Ninja build system found, using it for faster builds.${NC}"
else
  USE_NINJA=false
  echo -e "${YELLOW}Ninja build system not found, using default CMake generator.${NC}"
fi

# Configure the project
echo -e "${YELLOW}Configuring project with CMake (Build type: $BUILD_TYPE)...${NC}"

# Add extreme tests flag if requested
CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
if [ "$EXTREME_TEST" = true ]; then
  CMAKE_FLAGS="$CMAKE_FLAGS -DENABLE_EXTREME_TESTS=ON"
fi



if [ "$USE_NINJA" = true ]; then
  if [ "$VERBOSE" = true ]; then
    cmake -S . -B build $CMAKE_FLAGS -G Ninja
  else
    cmake -S . -B build $CMAKE_FLAGS -G Ninja > /dev/null
  fi
else
  if [ "$VERBOSE" = true ]; then
    cmake -S . -B build $CMAKE_FLAGS
  else
    cmake -S . -B build $CMAKE_FLAGS > /dev/null
  fi
fi

# Build the benchmark
echo -e "${YELLOW}Building AI Scaling Benchmark...${NC}"
if [ "$USE_NINJA" = true ]; then
  if [ "$VERBOSE" = true ]; then
    ninja -C build ai_scaling_benchmark
  else
    ninja -C build ai_scaling_benchmark > /dev/null
  fi
else
  if [ "$VERBOSE" = true ]; then
    cmake --build build --config $BUILD_TYPE --target ai_scaling_benchmark
  else
    cmake --build build --config $BUILD_TYPE --target ai_scaling_benchmark > /dev/null
  fi
fi

# Check if build was successful
if [ $? -ne 0 ]; then
  echo -e "${RED}Build failed. See output for details.${NC}"
  exit 1
fi

echo -e "${GREEN}Build successful!${NC}"

# Determine the correct path to the benchmark executable
if [ "$BUILD_TYPE" = "Debug" ]; then
  BENCHMARK_EXECUTABLE="./bin/debug/ai_scaling_benchmark"
else
  BENCHMARK_EXECUTABLE="./bin/release/ai_scaling_benchmark"
fi

# Verify executable exists
if [ ! -f "$BENCHMARK_EXECUTABLE" ]; then
  echo -e "${RED}Error: Benchmark executable not found at '$BENCHMARK_EXECUTABLE'${NC}"
  # Attempt to find the executable
  echo -e "${YELLOW}Searching for benchmark executable...${NC}"
  FOUND_EXECUTABLE=$(find ./bin -name "ai_scaling_benchmark*")
  if [ -n "$FOUND_EXECUTABLE" ]; then
    echo -e "${GREEN}Found executable at: $FOUND_EXECUTABLE${NC}"
    BENCHMARK_EXECUTABLE="$FOUND_EXECUTABLE"
  else
    echo -e "${RED}Could not find the benchmark executable. Build may have failed.${NC}"
    exit 1
  fi
fi

# Run the benchmark
echo -e "${YELLOW}Running AI Scaling Benchmark...${NC}"
echo -e "${YELLOW}This may take several minutes depending on your hardware.${NC}"
echo

# Create output file for results
RESULTS_FILE="test_results/ai_scaling_benchmark_$(date +%Y%m%d_%H%M%S).txt"
mkdir -p test_results

# Run the benchmark with output capturing
if [ "$VERBOSE" = true ]; then
  "$BENCHMARK_EXECUTABLE" --log_level=all | tee "$RESULTS_FILE"
else
  "$BENCHMARK_EXECUTABLE" | tee "$RESULTS_FILE"
fi

echo
echo -e "${GREEN}Benchmark complete!${NC}"
echo -e "${GREEN}Results saved to $RESULTS_FILE${NC}"

exit 0
