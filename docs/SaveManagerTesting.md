# SaveGameManager Testing Guide

## Overview

This document explains how to use the unit testing system for the SaveGameManager component in your SDL3_Template project. The tests use a standalone approach that avoids complex dependencies and focuses on validating the core functionality.

## Testing Structure

The testing system consists of:

1. **Unit Tests**: Located in the `tests` directory, these are a suite of automated tests that verify the functionality of the SaveGameManager.

2. **Mock Objects**: Standalone mock implementations (like MockPlayer) that simulate game objects without dependencies on the actual game classes.

3. **Test Implementation**: A dedicated `TestSaveGameManager` class that provides the core functionality for testing without relying on the actual SaveGameManager implementation.

4. **Test Runner**: A convenient script for building and running the tests.

## Why This Testing Approach?

The standalone testing approach offers several advantages:

1. **Independence**: Tests run without requiring other game systems to be working.

2. **Speed**: Tests compile and run quickly without complex dependencies.

3. **Reliability**: Tests focus on behavior validation, not implementation details.

4. **Isolation**: Issues in other parts of the codebase won't affect the test results.

## Running the Tests

### Using the Helper Script

The simplest way to run the tests is using the provided helper script:

```bash
# Regular run
./run_save_tests.sh

# Run with detailed output
./run_save_tests.sh --verbose
```

#### Cleaning and Rebuilding

If you make changes to the test code or SaveGameManager, use these options to rebuild:

```bash
# Clean just the test artifacts
./run_save_tests.sh --clean

# Remove the entire build directory and start fresh
./run_save_tests.sh --clean-all
```

## Understanding Test Results

### Normal Mode

In normal mode, you'll see a simple summary:

```
Running 3 test cases...
*** No errors detected
```

### Verbose Mode

In verbose mode (`--verbose`), you'll see detailed information about each test:

```
Entering test case "TestSaveAndLoad"
info: check saveResult has passed
info: check saveManager.saveExists("test_save.dat") has passed
...
Leaving test case "TestSaveAndLoad"; testing time: 6147us
```

## What's Being Tested

The tests cover the core functionality of save/load operations:

1. **Basic Save/Load**: Verifying that player state can be saved and loaded correctly.

2. **Slot Management**: Testing the slot-based save/load/delete operations.

3. **Error Handling**: Testing various error conditions like invalid files and null pointers.

## Adding New Tests

To add new tests for the SaveGameManager:

1. Open `tests/SaveManagerTests.cpp`

2. Add a new test case using the BOOST_AUTO_TEST_CASE macro:

```cpp
BOOST_AUTO_TEST_CASE(TestYourNewFeature) {
    // Create test manager
    TestSaveGameManager saveManager;
    
    // Setup test objects
    MockPlayer player;
    player.setTestPosition(100.0f, 200.0f);
    
    // Test your feature
    bool result = saveManager.yourNewMethod(&player);
    
    // Verify the result
    BOOST_CHECK(result);
}
```

3. Run the tests with `./run_save_tests.sh --clean` to ensure your changes are built.

## Extending the Tests

If you need to test new functionality:

1. Add the necessary methods to `TestSaveGameManager` in SaveManagerTests.cpp

2. If needed, enhance the `MockPlayer` class to support new operations

3. Create new test cases that use these extensions

## Best Practices

1. **Keep Tests Independent**: Each test should work on its own, not depending on other tests.

2. **Test One Thing at a Time**: Each test case should focus on testing a specific aspect.

3. **Keep Tests Focused**: Test the behavior, not the implementation details.

4. **Run Tests Regularly**: Run tests after making changes to catch issues early.

## Conclusion

The standalone testing approach provides a robust way to ensure your SaveGameManager's core functionality works correctly. This approach balances thoroughness with simplicity, making your tests more reliable and easier to maintain as your game evolves.