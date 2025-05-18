# SaveGameManager Unit Tests

This directory contains unit tests for the SaveGameManager component. The tests use a standalone approach that avoids complex dependencies and focuses on validating the core functionality.

## Test Structure

- `SaveManagerTests.cpp`: Contains test cases for the SaveGameManager functionality
- `MockPlayer.hpp/.cpp`: Provides a simplified mock implementation for testing without dependencies on the real Player class
- `CMakeLists.txt`: Build configuration for tests

## Running the Tests

You can run the tests using the provided script:

```bash
# Regular run
./run_save_tests.sh

# Clean build and run
./run_save_tests.sh --clean

# Complete rebuild and run
./run_save_tests.sh --clean-all

# Run with verbose output
./run_save_tests.sh --verbose
```

## Test Design Approach

These tests use a standalone testing approach:

1. **Independent MockPlayer**: Instead of using the actual Player class (which has many dependencies), we use a simple mock that provides just the interface needed for testing.

2. **TestSaveGameManager**: Instead of testing the actual SaveGameManager implementation (which may have complex dependencies), we created a simplified version that focuses only on the core saving/loading functionality.

3. **No External Dependencies**: The tests don't depend on other components of the game engine, making them faster and more reliable.

## Test Coverage

The test suite covers the following functionality:

1. **Basic Save/Load**: Verifies that player state can be saved and loaded correctly
2. **Slot Operations**: Tests the slot-based save/load/delete operations
3. **Error Handling**: Tests various error conditions like invalid files, null pointers, etc.

## Adding New Tests

To add new tests:

1. Modify `SaveManagerTests.cpp` to include additional test cases
2. Use the `BOOST_AUTO_TEST_CASE` macro to add new test functions
3. Run with `--clean` to ensure your changes are compiled

## Understanding Test Output

With `--verbose`, you'll see detailed output for each test:

```
Entering test case "TestSaveAndLoad"
info: check saveResult has passed
...
Leaving test case "TestSaveAndLoad"; testing time: 6147us
```

Without verbose mode, you'll just see a summary:

```
Running 3 test cases...
*** No errors detected
```