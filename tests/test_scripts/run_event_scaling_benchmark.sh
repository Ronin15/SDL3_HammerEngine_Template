#!/bin/bash

# EventManager Scaling Benchmark Test Script
# Copyright (c) 2025 Hammer Forged Games
# Licensed under the MIT License

set -e  # Exit on any error

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../../build"
RESULTS_DIR="$SCRIPT_DIR/../../test_results"
OUTPUT_FILE="$RESULTS_DIR/event_scaling_benchmark_output.txt"
BUILD_TYPE="debug"
VERBOSE=false

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to show help
show_help() {
    echo "EventManager Scaling Benchmark Test Script"
    echo ""
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --release     Run benchmark in release mode (optimized)"
    echo "  --verbose     Show detailed benchmark output"
    echo "  --clean       Clean build artifacts before building"
    echo "  --help        Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                    # Run benchmark with default settings"
    echo "  $0 --release         # Run optimized benchmark"
    echo "  $0 --verbose         # Show detailed performance metrics"
    echo "  $0 --clean --release # Clean build and run optimized benchmark"
    echo ""
    echo "Output:"
    echo "  Results are saved to: $OUTPUT_FILE"
    echo "  Console output shows real-time benchmark progress"
    echo ""
    echo "The benchmark tests EventManager performance across multiple scales:"
    echo "  - Basic handler performance (small scale)"
    echo "  - Medium scale performance (5K events, 25K handlers)"
    echo "  - Comprehensive scalability suite"
    echo "  - Concurrency testing (multi-threaded)"
    echo "  - Extreme scale testing (100K events, 5M handlers)"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --release)
            BUILD_TYPE="release"
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        --clean)
            print_status "Cleaning build artifacts..."
            if [ -d "$BUILD_DIR" ]; then
                rm -rf "$BUILD_DIR"
                print_success "Build directory cleaned"
            fi
            shift
            ;;
        --help)
            show_help
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Create results directory if it doesn't exist
mkdir -p "$RESULTS_DIR"

print_status "Starting EventManager Scaling Benchmark..."
print_status "Build type: $BUILD_TYPE"
print_status "Results will be saved to: $OUTPUT_FILE"

# Navigate to script directory
cd "$SCRIPT_DIR"

# Get the directory where this script is located and find project root
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Check if benchmark executable exists
BENCHMARK_EXEC="$PROJECT_ROOT/bin/$BUILD_TYPE/event_manager_scaling_benchmark"
if [ ! -f "$BENCHMARK_EXEC" ]; then
    print_error "Benchmark executable not found: $BENCHMARK_EXEC"
    # Attempt to find the executable
    FOUND_EXECUTABLE=$(find "$PROJECT_ROOT/bin" -name "event_manager_scaling_benchmark" -type f -executable | head -n 1)
    if [ -n "$FOUND_EXECUTABLE" ]; then
        print_info "Found executable at: $FOUND_EXECUTABLE"
        BENCHMARK_EXEC="$FOUND_EXECUTABLE"
    else
        print_error "Could not find the benchmark executable!"
        exit 1
    fi
fi

# Prepare output file
echo "EventManager Scaling Benchmark Results" > "$OUTPUT_FILE"
echo "=======================================" >> "$OUTPUT_FILE"
echo "Date: $(date)" >> "$OUTPUT_FILE"
echo "Build Type: $BUILD_TYPE" >> "$OUTPUT_FILE"
echo "System: $(uname -a)" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# Run the benchmark
print_status "Running EventManager scaling benchmark..."
print_status "This may take several minutes for comprehensive testing..."

if [ "$VERBOSE" = true ]; then
    print_status "Running with verbose output..."
    # Run with verbose output and save to file
    "$BENCHMARK_EXEC" 2>&1 | tee -a "$OUTPUT_FILE"
    BENCHMARK_RESULT=$?
else
    # Run quietly and save to file, show progress
    print_status "Running benchmark tests (use --verbose for detailed output)..."
    "$BENCHMARK_EXEC" >> "$OUTPUT_FILE" 2>&1 &
    BENCHMARK_PID=$!
    
    # Show progress while benchmark runs
    while kill -0 $BENCHMARK_PID 2>/dev/null; do
        echo -n "."
        sleep 2
    done
    echo ""
    
    wait $BENCHMARK_PID
    BENCHMARK_RESULT=$?
fi

# Check benchmark results
if [ $BENCHMARK_RESULT -eq 0 ]; then
    print_success "EventManager scaling benchmark completed successfully!"
    
    # Extract and display key performance metrics
    if [ -f "$OUTPUT_FILE" ]; then
        echo ""
        print_status "Performance Summary:"
        echo ""
        
        # Extract key metrics from the output
        grep -A 20 "EXTREME SCALE TEST" "$OUTPUT_FILE" | tail -15 || true
        
        echo ""
        print_status "Detailed results saved to: $OUTPUT_FILE"
        
        # Show file size for reference
        FILE_SIZE=$(du -h "$OUTPUT_FILE" | cut -f1)
        print_status "Output file size: $FILE_SIZE"
    fi
    
    echo ""
    print_success "EventManager scaling benchmark test completed!"
    
    if [ "$VERBOSE" = false ]; then
        echo ""
        print_status "For detailed performance metrics, run with --verbose flag"
        print_status "Or view the complete results: cat $OUTPUT_FILE"
    fi
    
    exit 0
else
    print_error "EventManager scaling benchmark failed with exit code: $BENCHMARK_RESULT"
    
    if [ -f "$OUTPUT_FILE" ]; then
        print_status "Check the output file for details: $OUTPUT_FILE"
        # Show last few lines of output for immediate debugging
        echo ""
        print_status "Last few lines of output:"
        tail -10 "$OUTPUT_FILE"
    fi
    
    exit $BENCHMARK_RESULT
fi