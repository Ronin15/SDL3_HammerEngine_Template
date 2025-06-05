#!/bin/bash

# UI Stress Test Runner Script
# Copyright (c) 2025 Hammer Forged Games
# All rights reserved.
# Licensed under the MIT License - see LICENSE file for details

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get the directory where this script is located and find project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Script configuration
BUILD_DIR="$PROJECT_ROOT/build"
TEST_EXECUTABLE="$PROJECT_ROOT/bin/debug/ui_stress_test"
LOG_DIR="$PROJECT_ROOT/test_results/ui_stress"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_FILE="$LOG_DIR/ui_stress_test_$TIMESTAMP.log"

# Test configuration options
STRESS_LEVEL="medium"  # light, medium, heavy, extreme
TEST_DURATION=30
MAX_COMPONENTS=500
ENABLE_MEMORY_STRESS=false
TEST_RESOLUTIONS=true
TEST_PRESENTATION_MODES=true
VERBOSE=false
SAVE_RESULTS=true
BENCHMARK_MODE=false

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

# Function to show usage
show_usage() {
    echo "UI Stress Test Runner"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -l, --level LEVEL        Stress test level (light|medium|heavy|extreme) [default: medium]"
    echo "  -d, --duration SECONDS   Test duration in seconds [default: 30]"
    echo "  -c, --components COUNT   Maximum components to create [default: 500]"
    echo "  -m, --memory-stress      Enable memory pressure testing [default: false]"
    echo "  -r, --skip-resolutions   Skip resolution scaling tests [default: false]"
    echo "  -p, --skip-presentation  Skip presentation mode tests [default: false]"
    echo "  -v, --verbose            Enable verbose output [default: false]"
    echo "  -s, --save-results       Save results to file [default: true]"
    echo "  -b, --benchmark          Run benchmark suite instead of stress tests [default: false]"
    echo "  -h, --help               Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                                    # Run medium stress test with defaults"
    echo "  $0 --level heavy --duration 60       # Run heavy stress test for 60 seconds"
    echo "  $0 --benchmark                       # Run benchmark suite"
    echo "  $0 --level light --memory-stress     # Run light test with memory pressure"
    echo ""
}

# Function to parse command line arguments
parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -l|--level)
                STRESS_LEVEL="$2"
                shift 2
                ;;
            -d|--duration)
                TEST_DURATION="$2"
                shift 2
                ;;
            -c|--components)
                MAX_COMPONENTS="$2"
                shift 2
                ;;
            -m|--memory-stress)
                ENABLE_MEMORY_STRESS=true
                shift
                ;;
            -r|--skip-resolutions)
                TEST_RESOLUTIONS=false
                shift
                ;;
            -p|--skip-presentation)
                TEST_PRESENTATION_MODES=false
                shift
                ;;
            -v|--verbose)
                VERBOSE=true
                shift
                ;;
            -s|--save-results)
                SAVE_RESULTS=true
                shift
                ;;
            -b|--benchmark)
                BENCHMARK_MODE=true
                shift
                ;;
            -h|--help)
                show_usage
                exit 0
                ;;
            *)
                print_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done
}

# Function to validate stress level
validate_stress_level() {
    case $STRESS_LEVEL in
        light|medium|heavy|extreme)
            return 0
            ;;
        *)
            print_error "Invalid stress level: $STRESS_LEVEL"
            print_error "Valid levels: light, medium, heavy, extreme"
            exit 1
            ;;
    esac
}

# Function to check prerequisites
check_prerequisites() {
    print_status "Checking prerequisites..."
    
    # Check if build directory exists
    if [ ! -d "$BUILD_DIR" ]; then
        print_error "Build directory not found: $BUILD_DIR"
        print_error "Please run cmake and build the project first"
        exit 1
    fi
    
    # Check if test executable exists
    if [ ! -f "$TEST_EXECUTABLE" ]; then
        print_error "Test executable not found: $TEST_EXECUTABLE"
        print_error "Please build the project first (make sure UI stress test target is built)"
        exit 1
    fi
    
    # Create log directory if it doesn't exist
    mkdir -p "$LOG_DIR"
    
    print_success "Prerequisites check passed"
}

# Function to build the project if needed
build_project() {
    print_status "Building project..."
    
    # Change to build directory
    cd "$BUILD_DIR"
    
    # Build the project
    if make -j$(nproc) > build.log 2>&1; then
        print_success "Project built successfully"
    else
        print_error "Build failed. Check $BUILD_DIR/build.log for details"
        exit 1
    fi
    
    # Return to project root directory
    cd "$PROJECT_ROOT"
}

# Function to run stress tests
run_stress_tests() {
    print_status "Starting UI stress tests..."
    print_status "Test Level: $STRESS_LEVEL"
    print_status "Duration: ${TEST_DURATION}s"
    print_status "Max Components: $MAX_COMPONENTS"
    print_status "Memory Stress: $ENABLE_MEMORY_STRESS"
    print_status "Test Resolutions: $TEST_RESOLUTIONS"
    print_status "Test Presentation Modes: $TEST_PRESENTATION_MODES"
    
    # Prepare test arguments
    TEST_ARGS="--stress-level $STRESS_LEVEL"
    TEST_ARGS="$TEST_ARGS --duration $TEST_DURATION"
    TEST_ARGS="$TEST_ARGS --max-components $MAX_COMPONENTS"
    
    if [ "$ENABLE_MEMORY_STRESS" = true ]; then
        TEST_ARGS="$TEST_ARGS --memory-stress"
    fi
    
    if [ "$TEST_RESOLUTIONS" = false ]; then
        TEST_ARGS="$TEST_ARGS --skip-resolutions"
    fi
    
    if [ "$TEST_PRESENTATION_MODES" = false ]; then
        TEST_ARGS="$TEST_ARGS --skip-presentation"
    fi
    
    if [ "$VERBOSE" = true ]; then
        TEST_ARGS="$TEST_ARGS --verbose"
    fi
    
    if [ "$SAVE_RESULTS" = true ]; then
        TEST_ARGS="$TEST_ARGS --save-results $LOG_FILE"
    fi
    
    # Run the tests
    print_status "Executing: $TEST_EXECUTABLE $TEST_ARGS"
    
    if [ "$VERBOSE" = true ]; then
        # Run with output to both console and log file
        "$TEST_EXECUTABLE" $TEST_ARGS 2>&1 | tee "$LOG_FILE"
        TEST_RESULT=${PIPESTATUS[0]}
    else
        # Run with output only to log file
        "$TEST_EXECUTABLE" $TEST_ARGS > "$LOG_FILE" 2>&1
        TEST_RESULT=$?
    fi
    
    if [ $TEST_RESULT -eq 0 ]; then
        print_success "UI stress tests completed successfully"
    else
        print_error "UI stress tests failed (exit code: $TEST_RESULT)"
        print_error "Check log file for details: $LOG_FILE"
        return 1
    fi
}

# Function to run benchmark suite
run_benchmark_suite() {
    print_status "Starting UI benchmark suite..."
    
    # Prepare benchmark arguments
    BENCHMARK_ARGS="--benchmark"
    
    if [ "$VERBOSE" = true ]; then
        BENCHMARK_ARGS="$BENCHMARK_ARGS --verbose"
    fi
    
    if [ "$SAVE_RESULTS" = true ]; then
        BENCHMARK_ARGS="$BENCHMARK_ARGS --save-results $LOG_FILE"
    fi
    
    # Run the benchmarks
    print_status "Executing: $TEST_EXECUTABLE $BENCHMARK_ARGS"
    
    if [ "$VERBOSE" = true ]; then
        # Run with output to both console and log file
        "$TEST_EXECUTABLE" $BENCHMARK_ARGS 2>&1 | tee "$LOG_FILE"
        BENCHMARK_RESULT=${PIPESTATUS[0]}
    else
        # Run with output only to log file
        "$TEST_EXECUTABLE" $BENCHMARK_ARGS > "$LOG_FILE" 2>&1
        BENCHMARK_RESULT=$?
    fi
    
    if [ $BENCHMARK_RESULT -eq 0 ]; then
        print_success "UI benchmark suite completed successfully"
    else
        print_error "UI benchmark suite failed (exit code: $BENCHMARK_RESULT)"
        print_error "Check log file for details: $LOG_FILE"
        return 1
    fi
}

# Function to display results summary
display_results_summary() {
    if [ -f "$LOG_FILE" ]; then
        print_status "Results Summary:"
        echo ""
        
        # Extract key metrics from log file
        if grep -q "=== UI Stress Test Results (Headless) ===" "$LOG_FILE"; then
            echo "Test Results Found:"
            grep -A 15 "=== UI Stress Test Results (Headless) ===" "$LOG_FILE" | head -16
        elif grep -q "=== UI Performance Benchmark Results (Headless) ===" "$LOG_FILE"; then
            echo "Benchmark Results Found:"
            grep -A 10 "=== UI Performance Benchmark Results (Headless) ===" "$LOG_FILE" | head -11
        else
            print_warning "No formatted results found in log file"
        fi
        
        echo ""
        print_status "Full results saved to: $LOG_FILE"
    else
        print_warning "No log file found"
    fi
}

# Function to check system resources
check_system_resources() {
    print_status "System Information:"
    
    # Get CPU info
    if command -v nproc >/dev/null 2>&1; then
        echo "CPU Cores: $(nproc)"
    fi
    
    # Get memory info
    if [ -f /proc/meminfo ]; then
        TOTAL_MEM=$(grep MemTotal /proc/meminfo | awk '{print int($2/1024)}')
        echo "Total Memory: ${TOTAL_MEM}MB"
    elif command -v sysctl >/dev/null 2>&1; then
        # macOS
        TOTAL_MEM=$(sysctl -n hw.memsize | awk '{print int($1/1024/1024)}')
        echo "Total Memory: ${TOTAL_MEM}MB"
    fi
    
    # Get GPU info if available
    GPU_DETECTED=false
    
    # Try NVIDIA first
    if command -v nvidia-smi >/dev/null 2>&1; then
        GPU_INFO=$(nvidia-smi --query-gpu=name --format=csv,noheader,nounits 2>/dev/null | head -1)
        if [ ! -z "$GPU_INFO" ]; then
            echo "GPU: $GPU_INFO"
            GPU_DETECTED=true
        fi
    fi
    
    # Try glxinfo for Linux (works with AMD, Intel, NVIDIA)
    if [ "$GPU_DETECTED" = false ] && command -v glxinfo >/dev/null 2>&1; then
        GPU_INFO=$(glxinfo 2>/dev/null | grep "OpenGL renderer" | cut -d: -f2 | sed 's/^ *//' | sed 's/ (.*$//' | head -1)
        if [ ! -z "$GPU_INFO" ]; then
            echo "GPU: $GPU_INFO"
            GPU_DETECTED=true
        fi
    fi
    
    # Try lspci as fallback for Linux
    if [ "$GPU_DETECTED" = false ] && command -v lspci >/dev/null 2>&1; then
        GPU_INFO=$(lspci 2>/dev/null | grep -E "(VGA|3D|Display)" | head -1 | cut -d: -f3 | sed 's/^ *//')
        if [ ! -z "$GPU_INFO" ]; then
            echo "GPU: $GPU_INFO"
            GPU_DETECTED=true
        fi
    fi
    
    # Try macOS detection
    if [ "$GPU_DETECTED" = false ] && command -v system_profiler >/dev/null 2>&1; then
        GPU_INFO=$(system_profiler SPDisplaysDataType 2>/dev/null | grep "Chipset Model" | head -1 | cut -d: -f2 | xargs)
        if [ ! -z "$GPU_INFO" ]; then
            echo "GPU: $GPU_INFO"
            GPU_DETECTED=true
        fi
    fi
    
    # If no GPU detected, show message
    if [ "$GPU_DETECTED" = false ]; then
        echo "GPU: Not detected"
    fi
    
    echo ""
}

# Function to clean up old test results
cleanup_old_results() {
    if [ -d "$LOG_DIR" ]; then
        # Remove test result files older than 7 days
        find "$LOG_DIR" -name "ui_stress_test_*.log" -mtime +7 -delete 2>/dev/null || true
        print_status "Cleaned up old test results"
    fi
}

# Main execution function
main() {
    echo "====================================="
    echo "    UI Stress Test Runner v1.0"
    echo "====================================="
    echo ""
    
    # Parse command line arguments
    parse_arguments "$@"
    
    # Validate arguments
    validate_stress_level
    
    # Show system information
    check_system_resources
    
    # Check prerequisites
    check_prerequisites
    
    # Clean up old results
    cleanup_old_results
    
    # Build project if needed
    # build_project
    
    # Run tests or benchmarks
    if [ "$BENCHMARK_MODE" = true ]; then
        if run_benchmark_suite; then
            print_success "Benchmark suite completed successfully"
        else
            print_error "Benchmark suite failed"
            exit 1
        fi
    else
        if run_stress_tests; then
            print_success "Stress tests completed successfully"
        else
            print_error "Stress tests failed"
            exit 1
        fi
    fi
    
    # Display results
    display_results_summary
    
    echo ""
    print_success "UI testing completed!"
    echo "====================================="
}

# Run main function with all arguments
main "$@"