# ThreadSystem Unit Tests

This directory contains unit tests for the ThreadSystem component. The tests validate the core functionality of the multi-threading system used throughout the game engine.

## Test Structure

- `ThreadSystemTests.cpp`: Contains test cases for the ThreadSystem functionality
- The tests run directly against the actual ThreadSystem implementation

## Running the Tests

You can run the tests using the provided script:

```bash
# Regular run
./run_thread_tests.sh

# Clean build and run
./run_thread_tests.sh --clean

# Complete rebuild and run
./run_thread_tests.sh --clean-all

# Run with verbose output
./run_thread_tests.sh --verbose
```

## Test Design Approach

These tests focus on the core ThreadSystem functionality:

1. **Thread Pool Initialization**: Verifies the thread pool initializes correctly with an appropriate number of threads.

2. **Task Execution**: Tests that tasks are properly executed on worker threads.

3. **Result Handling**: Ensures that tasks with results return their values correctly.

4. **Concurrency**: Validates that multiple tasks can run concurrently and that load balancing works.

5. **Error Handling**: Tests that exceptions thrown in tasks are properly propagated.

6. **Thread Isolation**: Ensures proper synchronization when accessing shared resources.

7. **System Management**: Tests busy flag reporting and system shutdown behaviors.

## Test Coverage

The test suite covers the following functionality:

1. **Thread Pool Initialization**: Tests that the system initializes with an appropriate number of threads.
2. **Simple Task Execution**: Validates that basic tasks execute correctly.
3. **Task With Result**: Tests that tasks can return values.
4. **Multiple Tasks**: Tests executing many tasks concurrently.
5. **Concurrent Task Results**: Ensures results from many concurrent tasks are correctly retrieved.
6. **Task Exceptions**: Tests error handling with exceptions in tasks.
7. **Concurrency Isolation**: Validates proper synchronization of shared resources.
8. **Busy Flag**: Tests that the system properly reports its busy status.
9. **Nested Tasks**: Tests that tasks can enqueue other tasks.
10. **Load Balancing**: Ensures that tasks are distributed across multiple threads.

## Adding New Tests

To add new tests:

1. Modify `ThreadSystemTests.cpp` to include additional test cases
2. Use the `BOOST_AUTO_TEST_CASE` macro to add new test functions
3. Run with `--clean` to ensure your changes are compiled

## Understanding Test Output

With `--verbose`, you'll see detailed output for each test:

```
Entering test case "TestThreadPoolInitialization"
info: check !Forge::ThreadSystem::Instance().isShutdown() has passed
...
Leaving test case "TestThreadPoolInitialization"; testing time: 143us
```

Without verbose mode, you'll just see a summary:

```
Running 10 test cases...
*** No errors detected
```

## Thread Safety Considerations

When adding tests, keep in mind:

- Avoid race conditions in test code
- Use appropriate synchronization (mutexes, atomic variables)
- Be careful with timing-dependent tests
- Consider using longer wait times to reduce flakiness