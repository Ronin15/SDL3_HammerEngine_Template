# Test Troubleshooting Guide

## Common Issues and Solutions

This guide addresses common issues you might encounter when working with our test suite. All tests now use the Boost Test Framework for consistency. Special attention is given to thread safety and proper cleanup of multithreaded tests.

### 1. "Multiple definition" or "Duplicate symbol" errors

**Problem**: You might get errors about multiple definitions of symbols or functions.

**Solution**: 
- Ensure the MockPlayer doesn't redefine member variables that are already in the base class
- Make sure there's only one definition of each function
- Use `inline` for functions defined in headers

### 2. Boost.Test configuration issues

**Problem**: Errors related to Boost.Test initialization or missing symbols.

**Solution**:
- Add `#define BOOST_TEST_MODULE YourModuleName` before including Boost.Test headers
- Use `#include <boost/test/included/unit_test.hpp>` for header-only approach
- Ensure the CMakeLists.txt correctly links with Boost.Test
- Use the correct macros for test cases: `BOOST_AUTO_TEST_CASE`, `BOOST_CHECK`, `BOOST_REQUIRE`, etc.
- For fixtures, use `BOOST_FIXTURE_TEST_CASE` or `BOOST_GLOBAL_FIXTURE`

### 3. File path or permission issues

**Problem**: Tests fail because they can't create or access files.

**Solution**:
- Make sure the test is using a temporary directory
- Ensure the test has write permission to the directory
- Use the static `saveDir` variable in TestFixture

### 4. Link errors with Boost

**Problem**: Linker errors related to Boost functions.

**Solution**:
- Make sure you're using the correct Boost library components
- Check that CMakeLists.txt properly links against Boost
- Use the `BOOST_TEST_NO_LIB` definition for header-only use

### 5. Issues with test framework compatibility

**Problem**: Test framework doesn't work with your build system.

**Solution**:
- Use `included/unit_test.hpp` instead of `unit_test.hpp` for header-only approach
- Add `#define BOOST_TEST_NO_LIB` before including Boost headers to avoid linking issues
- Use the appropriate Boost.Test macros for your version
- Make sure your test fixture properly initializes and cleans up resources

### 6. Thread-related segmentation faults during cleanup

**Problem**: Tests involving threads crash with signal 11 (segmentation fault) during cleanup.

**Solution**:
- Add `#define BOOST_TEST_NO_SIGNAL_HANDLING` before including Boost.Test headers
- Use `--catch_system_errors=no --no_result_code --detect_memory_leak=0` options when running tests
- Disable threading before cleanup: `AIManager::Instance().configureThreading(false)`
- Add sleep between operations: `std::this_thread::sleep_for(std::chrono::milliseconds(100))`
- Use timeout when waiting for futures: `future.wait_for(std::chrono::seconds(1))` instead of blocking `get()`
- Always clean up resources in reverse order of initialization
- Don't register SIGSEGV handler in your test code (let Boost.Test handle it)

## If you still have issues:

1. Clean the build completely: `./run_all_tests.sh --clean-all` or `./run_save_tests.sh --clean-all`
2. Run the tests with verbose output: `./bin/debug/save_manager_tests --log_level=all`
3. Check the console output for detailed error messages
4. Verify the test formatting by running: `./bin/debug/ai_optimization_tests --list_content`
5. For thread-safety tests, run with additional options: `--catch_system_errors=no --no_result_code`
6. Add diagnostic logging to your test code to trace resource creation and destruction
7. Ensure atomic operations use proper synchronization with `compare_exchange_strong` instead of simple `exchange`

## Making Changes to the Tests

If you need to modify the tests:

1. Update MockPlayer.hpp if you're changing player-related tests
2. Edit the test files to add or modify test cases
3. Follow the standard Boost.Test patterns shown in existing tests
4. Run with `--clean` to ensure changes are compiled: `./run_save_tests.sh --clean`

## Boost Test Framework Reference

For all tests, use the following structure:

```cpp
// Define module name and include Boost Test
#define BOOST_TEST_MODULE YourTestName
// For thread-safe tests, disable signal handling
#define BOOST_TEST_NO_SIGNAL_HANDLING
#include <boost/test/included/unit_test.hpp>

// Optional global fixture for setup/teardown
struct TestFixture {
    TestFixture() {
        // Setup code
        // For threaded tests, initialize in this order:
        // 1. ThreadSystem
        // 2. AIManager or other managers
        // 3. Enable threading
    }
    
    ~TestFixture() {
        // Cleanup code in reverse order:
        // 1. Disable threading
        // 2. Wait for threads to complete
        // 3. Clean up managers
        // 4. Clean up ThreadSystem
    }
};

BOOST_GLOBAL_FIXTURE(TestFixture);

// Define test cases
BOOST_AUTO_TEST_CASE(TestSomething) {
    // Test code
    BOOST_CHECK(condition);  // Continues test if failed
    BOOST_REQUIRE(condition);  // Stops test if failed
}
```

Common assertions:
- `BOOST_CHECK(condition)` - Continue test even if assertion fails
- `BOOST_REQUIRE(condition)` - Stop test if assertion fails
- `BOOST_CHECK_EQUAL(a, b)` - Check if a == b
- `BOOST_CHECK_NE(a, b)` - Check if a != b
- `BOOST_CHECK_CLOSE(a, b, tolerance)` - Check floating point approximate equality