# Forge Game Engine Test Suite

This directory contains the tests for the Forge Game Engine, along with scripts to run them. All tests use the Boost Test Framework for consistency.

## Available Test Suites

1. **AI Optimization Tests**: Tests performance optimizations in the AI system
2. **Save Manager Tests**: Tests save game functionality
3. **Thread System Tests**: Tests the multi-threading system

## Running Tests

### Windows

The following batch files are available to run tests:

- `run_all_tests.bat` - Runs all test suites and generates a summary report
- `run_ai_tests.bat` - Runs only the AI optimization tests
- `run_save_tests.bat` - Runs only the save manager tests
- `run_thread_tests.bat` - Runs only the thread system tests

These scripts are located in the project root directory.

These scripts will:
1. Build the necessary tests if they haven't been built
2. Run the tests and capture the output
3. Generate reports in the `test_results` directory

### Linux/macOS

Shell scripts are also available:

- `run_all_tests.sh` - Runs all test suites and generates a summary report
- `run_ai_tests.sh` - Runs the AI optimization tests
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
./run_ai_tests.sh --verbose  # Run with verbose output
```

## Test Reports

All test reports are saved in the `test_results` directory:

- `ai_test_output.txt` - Full output from AI tests
- `save_test_output.txt` - Full output from save manager tests
- `thread_test_output.txt` - Full output from thread system tests
- `summary_report_[timestamp].txt` - Summary of all test results
- `performance_metrics.txt` - AI performance measurements

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
4. See `TROUBLESHOOTING.md` for common issues