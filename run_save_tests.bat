@echo off
REM run_save_tests.bat - Script to build and run the save manager tests
REM Copyright (c) 2025 Hammer Forged Games

echo Building Save Manager Tests...

REM Navigate to project root directory (in case script is run from elsewhere)
cd /d "%~dp0"

REM Create results directory if it doesn't exist
if not exist "test_results" (
    echo Creating results directory...
    mkdir test_results
)

REM Create build directory if it doesn't exist
if not exist "build" (
    echo Creating build directory...
    mkdir build
    cmake -B build
)

REM Build the save manager tests
echo Compiling tests...
ninja -C build save_manager_tests

REM Check if build was successful
if %ERRORLEVEL% neq 0 (
    echo Build failed! Please fix compilation errors.
    exit /b 1
)

echo Build successful!

REM Run the tests and save output to file
echo Running Save Manager Tests...
bin\debug\save_manager_tests.exe > test_results\save_test_output.txt 2>&1

REM Check if tests were successful
if %ERRORLEVEL% neq 0 (
    echo Tests failed! See test_results\save_test_output.txt for details.
    echo Test failed with error code %ERRORLEVEL% >> test_results\save_test_output.txt
    exit /b 1
) else (
    echo All tests passed successfully!
    echo All tests completed successfully >> test_results\save_test_output.txt
)

echo Save Manager Test Summary:
echo 1. Tests save game data serialization
echo 2. Tests save file reading and writing
echo 3. Tests player data persistence
echo 4. Tests error handling for corrupted saves

echo Test completed at %DATE% %TIME% >> test_results\save_test_output.txt
echo.
echo Results saved to:
echo - test_results\save_test_output.txt (full log)

exit /b 0