@echo off
REM Script to run pathfinding system tests on Windows
REM This script runs comprehensive tests for the A* pathfinding system

setlocal EnableDelayedExpansion

REM Get the directory where this script is located
set SCRIPT_DIR=%~dp0
set PROJECT_DIR=%SCRIPT_DIR%..\..

REM Process command line arguments
set VERBOSE=false

:parse_args
if "%1"=="--verbose" (
    set VERBOSE=true
    shift
    goto parse_args
)
if "%1"=="--help" (
    echo Pathfinding System Tests Runner
    echo Usage: run_pathfinding_tests.bat [options]
    echo.
    echo Options:
    echo   --verbose    Run tests with verbose output
    echo   --help       Show this help message
    echo.
    echo Tests Coverage:
    echo   - Grid coordinate conversion and bounds checking
    echo   - A* pathfinding algorithm correctness
    echo   - Diagonal movement toggle functionality
    echo   - Dynamic weight system for avoidance
    echo   - Performance benchmarks for various grid sizes
    echo   - Edge cases and error handling
    exit /b 0
)

REM Create results directory
if not exist "%PROJECT_DIR%\test_results" mkdir "%PROJECT_DIR%\test_results"
set RESULTS_FILE=%PROJECT_DIR%\test_results\pathfinding_tests_results.txt

echo ======================================================
echo            Pathfinding System Tests
echo ======================================================

REM Check if the test executable exists
set TEST_EXECUTABLE=%PROJECT_DIR%\bin\debug\pathfinding_system_tests.exe

if not exist "%TEST_EXECUTABLE%" (
    echo Test executable not found: %TEST_EXECUTABLE%
    echo Make sure you have built the project with tests enabled.
    echo Run: cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug ^&^& ninja -C build
    exit /b 1
)

REM Run the tests
echo Running pathfinding system tests...
echo Pathfinding System Tests - %DATE% %TIME% > "%RESULTS_FILE%"

if "%VERBOSE%"=="true" (
    echo Verbose mode enabled
    "%TEST_EXECUTABLE%" --log_level=all --report_level=detailed --catch_system_errors=no >> "%RESULTS_FILE%" 2>&1
) else (
    "%TEST_EXECUTABLE%" --log_level=test_suite --report_level=detailed --catch_system_errors=no >> "%RESULTS_FILE%" 2>&1
)

set TEST_RESULT=%ERRORLEVEL%

echo. >> "%RESULTS_FILE%"
echo Test completed at: %DATE% %TIME% >> "%RESULTS_FILE%"
echo Exit code: %TEST_RESULT% >> "%RESULTS_FILE%"

REM Report results
echo.
echo ======================================================
if %TEST_RESULT%==0 (
    echo * All pathfinding system tests passed successfully!
    echo.
    echo Test Coverage:
    echo   * Grid coordinate system operations
    echo   * A* pathfinding algorithm
    echo   * Movement configuration ^(diagonal/orthogonal^)
    echo   * Dynamic weight system
    echo   * Performance benchmarks
    echo   * Error handling and edge cases
) else (
    echo X Some pathfinding system tests failed
    echo Check the detailed results in: %RESULTS_FILE%
)

echo Test results saved to: %RESULTS_FILE%
echo ======================================================

exit /b %TEST_RESULT%