# AI System Testing Guide

## Overview

The AI system tests verify the performance optimizations, core functionality, and thread safety of our AI architecture. These tests use the Boost Test Framework to ensure consistent testing methodology across the project.

## What's Being Tested

The AI optimization tests verify the following features:

1. **Entity Component Caching**: Tests faster entity-behavior lookups through optimized caching mechanisms
2. **Batch Processing**: Tests efficient batch processing of entities with similar behaviors
3. **Early Exit Conditions**: Tests skipping unnecessary updates based on update frequency
4. **Message Queue System**: Tests batched message processing for efficient AI communication
5. **Thread Safety**: Tests concurrent access to the AIManager from multiple threads

## Running the Tests

### Windows

```
run_ai_tests.bat
```

### Linux/macOS

```bash
chmod +x run_ai_tests.sh
./run_ai_tests.sh
```

Command-line options:
```bash
./run_ai_tests.sh --verbose  # Run with verbose output
./run_ai_tests.sh --clean    # Clean build before running
./run_ai_tests.sh --thread-safe  # Run thread safety tests
```

## Test Output

The test output will show performance metrics for each feature. A typical output includes:
- Batch processing time measurements
- Individual processing time measurements
- Entity counts and processing statistics
- Thread concurrency metrics

All test results are saved in `test_results/ai_test_output.txt` and performance metrics are extracted to `test_results/performance_metrics.txt`. Thread-safety test results are saved in `test_results/thread_safe_ai_test_output.txt`.

## Implementation Details

### Test Structure

The tests use Boost Test Framework with the following structure:

```cpp
// Define module name
#define BOOST_TEST_MODULE AIOptimizationTests
#include <boost/test/included/unit_test.hpp>

// Global fixture for setup/teardown
struct AITestFixture {
    AITestFixture() {
        // Initialize AI system
    }
    
    ~AITestFixture() {
        // Clean up AI system
    }
};

BOOST_GLOBAL_FIXTURE(AITestFixture);

// Test cases
BOOST_AUTO_TEST_CASE(TestEntityComponentCaching) {
    // Test code
}
```

### Thread-Safe Tests

The thread-safe tests use a similar structure but also initialize the ThreadSystem:

```cpp
struct ThreadedAITestFixture {
    ThreadedAITestFixture() {
        // Initialize the ThreadSystem
        Forge::ThreadSystem::Instance().init();
        
        // Initialize the AI system
        AIManager::Instance().init();
        
        // Enable threading for AIManager
        AIManager::Instance().configureThreading(true);
    }
};

BOOST_GLOBAL_FIXTURE(ThreadedAITestFixture);
```

### Mock Objects

The tests use mock implementations:
- `MockWanderBehavior`: A simplified behavior for testing AI update patterns
- `TestEntity`: A minimal entity implementation for behavior assignment
- `ThreadTestBehavior`: A behavior specifically designed for thread-safety testing

## Modifying the Tests

To add new AI tests:

1. Edit `AIOptimizationTest.cpp` for basic functionality tests
2. Edit `ThreadSafeAIManagerTest.cpp` for thread-safety tests
3. Add new test cases using `BOOST_AUTO_TEST_CASE`
4. Follow the existing patterns for performance measurement
5. Run the tests to verify your changes

## Troubleshooting

If AI tests fail:

1. Check that the AIManager singleton is properly initialized
2. Verify that behavior registration is working correctly
3. Check for proper cleanup between test cases
4. Run with verbose output: `./bin/debug/ai_optimization_tests --log_level=all`

For thread-safety tests:

1. Ensure ThreadSystem is properly initialized before AIManager
2. Check for proper mutex locking in the AIManager implementation
3. Look for potential race conditions in test failures
4. Run with verbose output: `./bin/debug/thread_safe_ai_manager_tests --log_level=all`

See `TROUBLESHOOTING.md` for more general testing issues.