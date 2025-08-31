@echo off
REM Script to run collision and pathfinding performance benchmarks on Windows
REM This script runs comprehensive performance tests for both systems

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
    echo Collision ^& Pathfinding Benchmark Runner
    echo Usage: run_collision_pathfinding_benchmark.bat [options]
    echo.
    echo Options:
    echo   --verbose    Run benchmarks with verbose output
    echo   --help       Show this help message
    echo.
    echo Benchmark Coverage:
    echo   - SpatialHash insertion performance ^(100-10K entities^)
    echo   - SpatialHash query performance with various entity densities
    echo   - SpatialHash update performance during movement
    echo   - Pathfinding grid performance ^(50x50 to 200x200^)
    echo   - Weighted pathfinding with avoidance areas
    echo   - Iteration limit impact on pathfinding performance
    echo.
    echo Estimated Runtime: 2-5 minutes
    exit /b 0
)

REM Create results directory
if not exist "%PROJECT_DIR%\test_results" mkdir "%PROJECT_DIR%\test_results"
set RESULTS_FILE=%PROJECT_DIR%\test_results\collision_pathfinding_benchmark_results.txt
set CSV_FILE=%PROJECT_DIR%\test_results\collision_pathfinding_benchmark.csv

echo ======================================================
echo       Collision ^& Pathfinding Benchmarks
echo ======================================================
echo Note: This benchmark may take several minutes to complete

REM Check if the test executable exists
set TEST_EXECUTABLE=%PROJECT_DIR%\bin\debug\collision_pathfinding_benchmark.exe

if not exist "%TEST_EXECUTABLE%" (
    echo Benchmark executable not found: %TEST_EXECUTABLE%
    echo Make sure you have built the project with tests enabled.
    echo Run: cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug ^&^& ninja -C build
    exit /b 1
)

REM Run the benchmarks
echo Starting collision and pathfinding benchmarks...
echo Collision ^& Pathfinding Benchmarks - %DATE% %TIME% > "%RESULTS_FILE%"

if "%VERBOSE%"=="true" (
    echo Verbose mode enabled
    "%TEST_EXECUTABLE%" --log_level=all >> "%RESULTS_FILE%" 2>&1
) else (
    "%TEST_EXECUTABLE%" --log_level=test_suite >> "%RESULTS_FILE%" 2>&1
)

set TEST_RESULT=%ERRORLEVEL%

echo. >> "%RESULTS_FILE%"
echo Benchmark completed at: %DATE% %TIME% >> "%RESULTS_FILE%"
echo Exit code: %TEST_RESULT% >> "%RESULTS_FILE%"

REM Report results
echo.
echo ======================================================
if %TEST_RESULT%==0 (
    echo * All collision and pathfinding benchmarks completed successfully!
    echo.
    echo Benchmark Coverage:
    echo   * SpatialHash insertion performance scaling
    echo   * SpatialHash query performance with entity density
    echo   * SpatialHash update performance during movement
    echo   * Pathfinding grid performance across sizes
    echo   * Weighted pathfinding with cost areas
    echo   * Iteration limit impact analysis
    
    if exist "%CSV_FILE%" (
        echo.
        echo Detailed CSV results generated: %CSV_FILE%
        echo Import this file into spreadsheet software for analysis
    )
) else (
    echo X Some benchmarks failed or encountered issues
    echo Check the detailed results in: %RESULTS_FILE%
)

echo.
echo Benchmark results saved to: %RESULTS_FILE%
echo ======================================================

REM Performance analysis and recommendations
if %TEST_RESULT%==0 (
    echo.
    echo Performance Analysis:
    echo Use the CSV file to identify:
    echo   • Optimal SpatialHash cell sizes for your entity density
    echo   • Entity count limits for maintaining target frame rates
    echo   • Pathfinding grid sizes suitable for real-time performance
    echo   • Impact of iteration limits on pathfinding success rates
    echo.
    echo Recommended Performance Targets:
    echo   • SpatialHash insertion: ^< 50μs per entity
    echo   • SpatialHash query: ^< 100μs per query
    echo   • SpatialHash update: ^< 75μs per update
    echo   • Pathfinding ^(100x100^): ^< 10ms per request
    echo   • Pathfinding ^(150x150^): ^< 20ms per request
)

exit /b %TEST_RESULT%