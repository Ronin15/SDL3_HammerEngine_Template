@echo off
REM run_thread_tests.bat - Script to run the thread system tests
REM Copyright (c) 2025 Hammer Forged Games

REM Process command line arguments
set VERBOSE=false

:parse_args
if "%~1"=="" goto :end_parse_args
if /i "%~1"=="--verbose" (
    set VERBOSE=true
    shift
    goto :parse_args
)
if /i "%~1"=="--help" (
    echo ThreadSystem Test Runner
    echo Usage: run_thread_tests.bat [options]
    echo.
    echo Options:
    echo   --verbose    Run tests with verbose output
    echo   --help       Show this help message
    exit /b 0
)
shift
goto :parse_args
:end_parse_args

echo Running Thread System Tests...

REM Navigate to project root directory (in case script is run from elsewhere)
cd /d "%~dp0"

REM Create results directory if it doesn't exist
if not exist "test_results" (
    echo Creating results directory...
    mkdir test_results
)

REM Run the tests and save output to file
if "%VERBOSE%"=="true" (
    bin\debug\thread_system_tests.exe --log_level=all > test_results\thread_test_output.txt 2>&1
) else (
    bin\debug\thread_system_tests.exe > test_results\thread_test_output.txt 2>&1
)

REM Check if tests were successful
if %ERRORLEVEL% neq 0 (
    echo Tests failed! See test_results\thread_test_output.txt for details.
    echo Test failed with error code %ERRORLEVEL% >> test_results\thread_test_output.txt
    exit /b 1
) else (
    echo All tests passed successfully!
    echo All tests completed successfully >> test_results\thread_test_output.txt
)

echo Thread System Test Summary:
echo 1. Tests task creation and execution
echo 2. Tests multithreaded workloads
echo 3. Tests task scheduling and prioritization
echo 4. Tests thread safety mechanisms

echo Test completed at %DATE% %TIME% >> test_results\thread_test_output.txt
echo.
echo Results saved to:
echo - test_results\thread_test_output.txt (full log)

exit /b 0