#!/bin/bash

# Resource Edge Case Test Runner
# Runs comprehensive edge case tests for the resource system

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
TEST_BINARY="$PROJECT_ROOT/bin/debug/resource_edge_case_tests"

echo "Running Resource System Edge Case Tests..."
echo "=========================================="

# Check if test binary exists
if [ ! -f "$TEST_BINARY" ]; then
    echo "ERROR: Test binary not found at $TEST_BINARY"
    echo "Please build the tests first with: ninja -C build resource_edge_case_tests"
    exit 1
fi

# Run tests with timeout to prevent hanging on concurrent tests
echo "Executing edge case tests with 60 second timeout..."
timeout 60 "$TEST_BINARY" --log_level=error --report_level=short

TEST_RESULT=$?

if [ $TEST_RESULT -eq 0 ]; then
    echo ""
    echo "✅ All edge case tests PASSED!"
    echo ""
    echo "Resource system successfully validated against:"
    echo "• Handle lifecycle edge cases (overflow, stale handles)"
    echo "• Concurrent access patterns and race conditions"  
    echo "• Memory pressure and resource exhaustion"
    echo "• Malformed input and error recovery"
    echo "• Performance under extreme load conditions"
    echo "• System integration edge cases"
elif [ $TEST_RESULT -eq 124 ]; then
    echo ""
    echo "⚠️  Tests TIMED OUT after 60 seconds"
    echo "This may indicate performance issues in concurrent tests"
else
    echo ""
    echo "❌ Some tests FAILED (exit code: $TEST_RESULT)"
    echo "Check the output above for details"
fi

exit $TEST_RESULT