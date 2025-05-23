# Forge Game Engine Test Suite

This directory contains the tests for the Forge Game Engine, along with scripts to run them. All tests use the Boost Test Framework for consistency.

> **New Documentation**: For detailed information about all tests, see [TESTING.md](TESTING.md)

## Available Test Suites

1. **AI Optimization Tests**: Tests performance optimizations in the AI system
2. **Thread-Safe AI Tests**: Tests thread safety of the AI management system
3. **Thread-Safe AI Integration Tests**: Tests the integration of thread-safe AI components
4. **AI Benchmark Tests**: Performance benchmarking for AI scaling capabilities
5. **Save Manager Tests**: Tests save game functionality
6. **Thread System Tests**: Tests the multi-threading system

## Running Tests

### Windows

The following batch files are available to run tests:

- `run_ai_optimization_tests.bat` - Runs the AI optimization tests
- `run_thread_safe_ai_tests.bat` - Runs the thread-safe AI manager tests
- `run_thread_safe_ai_integration_tests.bat` - Runs the thread-safe AI integration tests
- `run_ai_benchmark.bat` - Runs the AI scaling benchmark tests
- `run_save_tests.bat` - Runs only the save manager tests
- `run_thread_tests.bat` - Runs only the thread system tests

These scripts are located in the project root directory.

These scripts will:
1. Build the necessary tests if they haven't been built
2. Run the tests and capture the output
3. Generate reports in the `test_results` directory

### Linux/macOS

Shell scripts are also available:

- `run_ai_optimization_tests.sh` - Runs the AI optimization tests
- `run_thread_safe_ai_tests.sh` - Runs the thread-safe AI manager tests
- `run_thread_safe_ai_integration_tests.sh` - Runs the thread-safe AI integration tests
- `run_ai_benchmark.sh` - Runs the AI scaling benchmark tests
- `run_save_tests.sh` - Runs only the save manager tests
- `run_thread_tests.sh` - Runs only the thread system tests

To run a shell script, make it executable first:

```bash
chmod +x run_all_tests.sh
./run_all_tests.sh
```

The shell scripts support various command-line options:

```bash
./run_save_tests.sh --help  # Show help message
./run_thread_tests.sh --clean  # Clean test artifacts before building
./run_ai_optimization_tests.sh --verbose  # Run with verbose output
./run_thread_safe_ai_tests.sh --release  # Run in release mode
./run_ai_benchmark.sh --extreme  # Run extreme benchmark tests
```

## Test Reports

All test reports are saved in the `test_results` directory:

- `ai_optimization_tests_output.txt` - Output from AI optimization tests
- `ai_optimization_tests_performance_metrics.txt` - Performance metrics from AI optimization tests
- `thread_safe_ai_test_output.txt` - Output from thread-safe AI manager tests
- `thread_safe_ai_performance_metrics.txt` - Performance metrics from thread-safe AI tests
- `ai_scaling_benchmark_[timestamp].txt` - Results from AI scaling benchmark
- `save_test_output.txt` - Full output from save manager tests
- `thread_test_output.txt` - Full output from thread system tests

When running tests with the `--verbose` flag on Linux/macOS, additional diagnostic information will be displayed but not saved to these files.

## Adding New Tests

1. Create your test file in this directory
2. Add it to `tests/CMakeLists.txt`
3. Create a batch/shell script to run your test suite

## Test Framework

All tests in this project use the Boost Test Framework. This standardization ensures consistency across tests and simplifies maintenance. Key aspects of our test approach:

- Header-only Boost.Test configuration for easier setup
- Global fixtures for test setup and teardown
- Standard assertion macros like BOOST_CHECK and BOOST_REQUIRE
- Consistent reporting format across all test suites

## Test Suites Details

### AI Optimization Tests

The AI optimization tests verify the following features:

1. **Entity Component Caching**: Tests faster entity-behavior lookups
2. **Batch Processing**: Tests efficient batch processing of entities
3. **Early Exit Conditions**: Tests skipping unnecessary updates
4. **Message Queue System**: Tests batched message processing

### Thread-Safe AI Tests

The thread-safe AI tests verify the following features:

1. **Thread-Safe Behavior Registration**: Tests concurrent registration of behaviors
2. **Thread-Safe Entity Assignment**: Tests assigning behaviors to entities from multiple threads
3. **Concurrent Behavior Processing**: Tests running AI behaviors across multiple threads
4. **Thread-Safe Cache Invalidation**: Tests optimization cache invalidation in multi-threaded context
5. **Thread-Safe Messaging**: Tests the message queue system with concurrent access

### AI Benchmark Tests

The AI benchmark tests measure:

1. **Threading Performance**: Compares single-threaded vs multi-threaded performance
2. **Scalability**: Tests how AI performance scales with different entity counts
3. **Behavior Complexity**: Measures impact of behavior complexity on performance
4. **Thread Count Impact**: Evaluates performance with different numbers of threads

### Save Manager Tests

The save manager tests verify the following features:

1. **Data Serialization**: Tests correct serialization of game objects
2. **File Operations**: Tests reading from and writing to save files
3. **Error Handling**: Tests recovery from corrupted save data
4. **Versioning**: Tests compatibility between different save formats

### Thread System Tests

The thread system tests verify the following features:

1. **Task Scheduling**: Tests proper scheduling and execution of tasks
2. **Thread Safety**: Tests synchronization mechanisms
3. **Performance**: Tests scaling with different numbers of threads
4. **Error Handling**: Tests recovery from failed tasks

## Troubleshooting

If tests fail:

1. Check the detailed logs in the `test_results` directory
2. Ensure all dependencies are properly installed
3. Verify the build environment is correctly set up
4. For thread-related tests, use `--catch_system_errors=no` option
5. For AI thread tests, check `AI_TESTING.md` for specific guidance
6. See `TROUBLESHOOTING.md` for common issues and solutions

## Additional Documentation

- `TESTING.md` - **Comprehensive guide** to all test suites, setup, and troubleshooting
- `TROUBLESHOOTING.md` - Common issues and their solutions

**Note**: The previous separate documentation files (`AI_TESTING.md`, `THREAD_TESTING.md`, and `SAVEGAME_TESTING.md`) have been consolidated into the comprehensive `TESTING.md` guide.