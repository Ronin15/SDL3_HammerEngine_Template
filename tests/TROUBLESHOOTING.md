# SaveGameManager Test Troubleshooting

## Common Issues and Solutions

If you encounter issues with the SaveGameManager tests, here are some common problems and their solutions:

### 1. "Multiple definition" or "Duplicate symbol" errors

**Problem**: You might get errors about multiple definitions of symbols or functions.

**Solution**: 
- Ensure the MockPlayer doesn't redefine member variables that are already in the base class
- Make sure there's only one definition of each function
- Use `inline` for functions defined in headers

### 2. Boost.Test configuration issues

**Problem**: Errors related to Boost.Test initialization or missing symbols.

**Solution**:
- Use the provided Boost.Test configuration in `SaveManagerTests.cpp`
- Ensure the CMakeLists.txt correctly links with Boost.Test
- Use the correct macros for test cases

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
- Provide a custom main function as shown in the test file
- Use the appropriate Boost.Test macros for your version

## If you still have issues:

1. Clean the build completely: `./run_save_tests.sh --clean-all`
2. Run the tests with verbose output: `./build/tests/save_manager_tests --log_level=all`
3. Check the console output for detailed error messages

## Making Changes to the Tests

If you need to modify the tests:

1. Update MockPlayer.hpp if you're changing player-related tests
2. Edit SaveManagerTests.cpp to add or modify test cases
3. Run with `--clean` to ensure changes are compiled: `./run_save_tests.sh --clean`