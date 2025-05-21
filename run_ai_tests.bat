@echo off
REM run_ai_tests.bat - Script to build and run the AI optimization tests
REM Copyright (c) 2025 Hammer Forged Games

echo Building AI Optimization Tests...

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

REM Build the AI tests
echo Compiling tests...
ninja -C build ai_optimization_tests

REM Check if build was successful
if %ERRORLEVEL% neq 0 (
    echo Build failed! Please fix compilation errors.
    exit /b 1
)

echo Build successful!

REM Run the tests and save output to file
echo Running AI Optimization Tests...
bin\debug\ai_optimization_tests.exe > test_results\ai_test_output.txt 2>&1

REM Check if tests were successful
if %ERRORLEVEL% neq 0 (
    echo Tests failed! See test_results\ai_test_output.txt for details.
    echo Test failed with error code %ERRORLEVEL% >> test_results\ai_test_output.txt
    exit /b 1
) else (
    echo All tests passed successfully!
    echo All tests completed successfully >> test_results\ai_test_output.txt
)

echo Performance Summary:
echo 1. Entity Component Caching: Reduces map lookups for faster entity-behavior access
echo 2. Batch Processing: Groups similar entities for better cache coherency
echo 3. Early Exit Conditions: Skips unnecessary updates based on custom conditions
echo 4. Message Queue System: Batches messages for efficient delivery

REM Extract just the timing information from the test output
echo Extracting performance metrics...
findstr "processing time" test_results\ai_test_output.txt > test_results\performance_metrics.txt
echo Test completed at %DATE% %TIME% >> test_results\performance_metrics.txt
echo.
echo Results saved to:
echo - test_results\ai_test_output.txt (full log)
echo - test_results\performance_metrics.txt (timing information only)

exit /b 0