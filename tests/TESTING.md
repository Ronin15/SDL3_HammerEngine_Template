# Forge Game Engine Test Framework

This document provides a comprehensive guide to the testing framework used in the Forge Game Engine project. All tests use the Boost Test Framework for consistency and are organized by component.

## Test Suites Overview

The Forge Game Engine has the following test suites:

1. **AI System Tests**
   - AI Optimization Tests: Verify performance optimizations in the AI system
   - Thread-Safe AI Tests: Validate thread safety of the AI management system
   - Thread-Safe AI Integration Tests: Test integration of AI components with threading
   - AI Benchmark Tests: Measure performance characteristics and scaling capabilities

2. **Core Systems Tests**
   - Save Manager Tests: Validate save/load functionality
   - Thread System Tests: Verify multi-threading capabilities

## Running Tests

### Available Test Scripts

Each test suite has dedicated scripts in the project root directory:

#### Linux/macOS
```bash
./run_ai_optimization_tests.sh       # AI optimization tests
./run_thread_safe_ai_tests.sh        # Thread-safe AI tests
./run_thread_safe_ai_integration_tests.sh  # Thread-safe AI integration tests
./run_ai_benchmark.sh                # AI scaling benchmark
./run_save_tests.sh                  # Save manager tests
./run_thread_tests.sh                # Thread system tests
./run_all_tests.sh                   # Run all test scripts sequentially
```

#### Windows
```
run_ai_optimization_tests.bat        # AI optimization tests
run_thread_safe_ai_tests.bat         # Thread-safe AI tests
run_thread_safe_ai_integration_tests.bat  # Thread-safe AI integration tests
run_ai_benchmark.bat                 # AI scaling benchmark
run_save_tests.bat                   # Save manager tests
run_thread_tests.bat                 # Thread system tests
run_all_tests.bat                    # Run all test scripts sequentially
```

### Common Command-Line Options

All test scripts support these options:

| Option | Description |
|--------|-------------|
| `--verbose` | Show detailed test output |
| `--release` | Run tests in release mode (optimized) |
| `--help` | Show help message for the script |

Special options:
- `--extreme` for AI benchmark (runs extended benchmarks)

## Test Output

Test results are saved in the `test_results` directory:

- `ai_optimization_tests_output.txt` - Output from AI optimization tests
- `ai_optimization_tests_performance_metrics.txt` - Performance metrics from optimization tests
- `thread_safe_ai_test_output.txt` - Output from thread-safe AI tests
- `thread_safe_ai_performance_metrics.txt` - Performance metrics from thread-safe AI tests
- `ai_scaling_benchmark_[timestamp].txt` - AI scaling benchmark results
- `save_test_output.txt` - Output from save manager tests
- `thread_test_output.txt` - Output from thread system tests

When using the `run_all_tests` scripts, combined results are also saved:

- `combined/all_tests_results.txt` - Summary of all test script results

## Test Implementation Details

### AI Optimization Tests

Located in `AIOptimizationTest.cpp`, these tests verify:

1. **Entity Component Caching**: Tests caching mechanisms for faster entity-behavior lookups
2. **Batch Processing**: Validates efficient batch processing of entities with similar behaviors
3. **Early Exit Conditions**: Tests optimizations that skip unnecessary updates
4. **Message Queue System**: Verifies batched message processing for efficient AI communication

### Thread-Safe AI Tests

Located in `ThreadSafeAIManagerTest.cpp`, these tests verify:

1. **Thread-Safe Behavior Registration**: Tests concurrent registration of behaviors
2. **Thread-Safe Entity Assignment**: Validates behavior assignment from multiple threads
3. **Concurrent Behavior Processing**: Tests running AI behaviors across multiple threads
4. **Thread-Safe Cache Invalidation**: Validates optimization cache in multi-threaded context
5. **Thread-Safe Messaging**: Tests message queuing with concurrent access

Special considerations for thread-safety tests:
- Use atomic operations with proper synchronization
- Disable threading before cleanup to prevent segmentation faults
- Allow time between operations for thread synchronization
- Use timeout when waiting for futures to prevent hanging

### AI Benchmark Tests

Located in `AIScalingBenchmark.cpp`, these tests measure:

1. **Threading Performance**: Compares single-threaded vs. multi-threaded performance
2. **Scalability**: Tests how AI performance scales with different entity counts
3. **Behavior Complexity**: Measures impact of behavior complexity on performance
4. **Thread Count Impact**: Evaluates performance with different numbers of worker threads

### Save Manager Tests

Located in `SaveManagerTests.cpp` with supporting `MockPlayer` class and a robust directory testing framework, these tests verify:

1. **Data Serialization**: Tests serialization of game objects
2. **File Operations**: Tests reading from and writing to save files
3. **Error Handling**: Tests recovery from corrupted save data
4. **Versioning**: Tests compatibility between different save formats
5. **Directory Creation**: Tests proper creation of save directories, including:
   - Creation of base directories when they don't exist
   - Creation of nested subdirectories (e.g., `game_saves` within a base directory)
   - Verification of directory write permissions
   - Path handling and validation across different operating systems
   - Recovery from directory creation failures
   - Working directory awareness and path resolution

### Thread System Tests

Located in `ThreadSystemTests.cpp`, these tests verify:

1. **Task Scheduling**: Tests scheduling and execution of tasks
2. **Thread Safety**: Tests synchronization mechanisms
3. **Performance**: Tests scaling with different numbers of threads
4. **Error Handling**: Tests recovery from failed tasks

## Adding New Tests

1. Choose the appropriate test file for your component
2. Add test cases using the `BOOST_AUTO_TEST_CASE` macro
3. Follow the existing pattern for setup, execution, and verification
4. Run tests with `--clean` to ensure your changes are compiled
5. For directory or file operation tests, always clean up created resources when tests complete

For directory management tests (like in SaveManagerTests):
- Use a dedicated test directory that's different from production directories
- Implement proper cleanup in test class destructors or at the end of test cases
- Add detailed logging to identify filesystem operation failures
- Test both successful cases and error cases (e.g., permission denied, disk full)

### Basic Test Structure

```cpp
// Define module name
#define BOOST_TEST_MODULE YourTestName
#include <boost/test/included/unit_test.hpp>

// For thread-safe tests, disable signal handling
#define BOOST_TEST_NO_SIGNAL_HANDLING

// Global fixture for setup/teardown
struct TestFixture {
    TestFixture() {
        // Setup code
    }
    
    ~TestFixture() {
        // Cleanup code
    }
};

BOOST_GLOBAL_FIXTURE(TestFixture);

// Test cases
BOOST_AUTO_TEST_CASE(TestSomething) {
    // Test code
    BOOST_CHECK(condition);  // Continues test if failed
    BOOST_REQUIRE(condition);  // Stops test if failed
}
```

## Thread Safety Considerations

For thread-safety tests, follow these guidelines:

1. **Test Initialization Order**
   - Initialize ThreadSystem first, then other systems
   - Enable threading only after initialization is complete

2. **Test Cleanup Order**
   - Disable threading before cleanup
   - Wait for threads to complete with appropriate timeouts
   - Clean up managers before cleaning up ThreadSystem

3. **Preventing Deadlocks and Race Conditions**
   - Use atomic operations with proper synchronization
   - Add sleep between operations to allow threads to complete
   - Use timeouts when waiting for futures instead of blocking calls
   - Use `compare_exchange_strong` instead of simple `exchange` for atomics

4. **Boost Test Options**
   - Add `#define BOOST_TEST_NO_SIGNAL_HANDLING` before including Boost.Test
   - Use `--catch_system_errors=no --no_result_code --detect_memory_leak=0` test options

## Troubleshooting Common Issues

### Boost.Test Configuration Issues

**Problem**: Errors related to Boost.Test initialization or missing symbols.

**Solution**:
- Add `#define BOOST_TEST_MODULE YourModuleName` before including Boost.Test
- Use `#include <boost/test/included/unit_test.hpp>` for header-only approach
- Check CMakeLists.txt has correct Boost linking
- Use correct test macros (`BOOST_AUTO_TEST_CASE`, `BOOST_CHECK`, etc.)

### Thread-Related Segmentation Faults

**Problem**: Tests crash with signal 11 (segmentation fault) during cleanup.

**Solution**:
- Add `#define BOOST_TEST_NO_SIGNAL_HANDLING` before including Boost.Test
- Run with `--catch_system_errors=no --no_result_code --detect_memory_leak=0`
- Disable threading before cleanup: `AIManager::Instance().configureThreading(false)`
- Add sleep between operations: `std::this_thread::sleep_for(std::chrono::milliseconds(100))`
- Use timeout for futures: `future.wait_for(std::chrono::seconds(1))`
- Don't register SIGSEGV handler in test code

### Filesystem Operation Issues

**Problem**: Directory creation, file operations, or save/load functions fail during tests.

**Solution**:
- Check working directory: Tests might run from a different directory than expected
- Print and verify absolute paths: `std::filesystem::absolute(path).string()`
- Ensure parent directories exist before creating files
- Verify write permissions on directories with a small test file
- Use detailed logging to identify exactly which operation is failing
- Add proper error handling with try/catch blocks around all filesystem operations
- Always clean up test files/directories in both success and failure scenarios
- On Windows, ensure paths don't exceed MAX_PATH (260 characters)

### Build Issues

**Problem**: Tests won't build or can't find dependencies.

**Solution**:
- Run with `--clean` to ensure clean rebuilding
- Check that all required libraries are installed and linked in CMakeLists.txt
- Check proper include paths are set in CMakeLists.txt

## Running All Tests

The `run_all_tests` scripts provide a convenient way to run all test suites sequentially:

### Linux/macOS
```bash
./run_all_tests.sh [options]
```

### Windows
```
run_all_tests.bat [options]
```

These scripts:
1. Run each test script one by one, giving them time to complete
2. Pass along the `--verbose` option to individual test scripts if specified
3. Generate a summary showing which tests passed or failed
4. Save combined results to `test_results/combined/all_tests_results.txt`
5. Return a non-zero exit code if any tests fail

## CMake Configuration

Tests are configured in `tests/CMakeLists.txt` with the following structure:

1. Define test executables with source files
2. Set compiler definitions for Boost.Test
3. Link necessary libraries
4. Register tests with CTest

For thread-safe tests, ensure `BOOST_TEST_NO_SIGNAL_HANDLING` is defined.