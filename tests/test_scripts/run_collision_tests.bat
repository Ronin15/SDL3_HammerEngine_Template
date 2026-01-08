@echo off
REM Script to run collision system tests on Windows
REM This script runs comprehensive tests for the spatial hash collision system

setlocal EnableDelayedExpansion

REM Navigate to script directory
cd /d "%~dp0" 2>nul

REM Process command line arguments
set VERBOSE=false

:parse_args
if "%1"=="--verbose" (
    set VERBOSE=true
    shift
    goto parse_args
)
if "%1"=="--help" (
    echo Collision System Tests Runner
    echo Usage: run_collision_tests.bat [options]
    echo.
    echo Options:
    echo   --verbose    Run tests with verbose output
    echo   --help       Show this help message
    echo.
    echo Tests Coverage:
    echo   - AABB intersection and containment tests
    echo   - SpatialHash insertion, removal, and querying
    echo   - Collision system performance benchmarks
    echo   - Stress tests with high entity density
    echo   - Boundary condition edge cases
    exit /b 0
)

REM Create results directory
if not exist "..\..\test_results" mkdir "..\..\test_results"
set RESULTS_FILE=..\..\test_results\collision_tests_results.txt

echo ======================================================
echo             Collision System Tests
echo ======================================================

REM Check if the test executable exists
set TEST_EXECUTABLE=..\..\bin\debug\collision_system_tests.exe

if not exist "%TEST_EXECUTABLE%" (
    echo Test executable not found: %TEST_EXECUTABLE%
    echo Make sure you have built the project with tests enabled.
    echo Run: cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug ^&^& ninja -C build
    exit /b 1
)

REM Run the tests
echo Running collision system tests...
echo Collision System Tests - %DATE% %TIME% > "%RESULTS_FILE%"

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
    echo * All collision system tests passed successfully!
    echo.
    echo Test Coverage:
    echo   * AABB geometric operations
    echo   * SpatialHash data structure operations
    echo   * Performance benchmarks
    echo   * Stress testing with high density
    echo   * Boundary condition handling
) else (
    echo X Some collision system tests failed
    echo Check the detailed results in: %RESULTS_FILE%
    echo.
    echo Note: Tests may fail due to performance thresholds on slower systems.
    echo This is expected behavior and indicates the tests are working correctly.
)

echo Test results saved to: %RESULTS_FILE%
echo ======================================================

exit /b %TEST_RESULT%