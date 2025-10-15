@echo off
REM Script to run collision system performance benchmarks on Windows
REM This script runs comprehensive collision performance tests

setlocal enabledelayedexpansion

REM Navigate to script directory
cd /d "%~dp0"

REM Get the directory where this script is located
set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%..\.."

REM Process command line arguments
set "VERBOSE=false"

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--verbose" (
    set "VERBOSE=true"
    shift
    goto :parse_args
)
if /i "%~1"=="--help" (
    echo Collision System Benchmark Runner
    echo Usage: run_collision_benchmark.bat [options]
    echo.
    echo Options:
    echo   --verbose    Run benchmarks with verbose output
    echo   --help       Show this help message
    echo.
    echo Benchmark Coverage:
    echo   - CollisionManager SOA storage performance
    echo   - SpatialHash insertion performance (100-10K entities^)
    echo   - SpatialHash query performance with various entity densities
    echo   - SpatialHash update performance during movement
    echo   - Broadphase and narrowphase collision detection scaling
    echo   - Threading vs single-threaded performance comparison
    echo.
    echo Estimated Runtime: 2-3 minutes
    goto :eof
)
echo Unknown option: %~1
echo Use --help for usage information
exit /b 1

:done_parsing

REM Create results directory
if not exist "%PROJECT_DIR%\test_results" mkdir "%PROJECT_DIR%\test_results"
set "RESULTS_FILE=%PROJECT_DIR%\test_results\collision_benchmark_results.txt"
set "CSV_FILE=%PROJECT_DIR%\test_results\collision_benchmark.csv"

echo ======================================================
echo            Collision System Benchmarks
echo ======================================================
echo Note: This benchmark may take several minutes to complete

REM Check if the test executable exists
set "TEST_EXECUTABLE=%PROJECT_DIR%\bin\debug\collision_benchmark.exe"

if not exist "%TEST_EXECUTABLE%" (
    echo Benchmark executable not found: %TEST_EXECUTABLE%
    echo Make sure you have built the project with tests enabled.
    echo Run: cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug ^&^& ninja -C build
    exit /b 1
)

REM Run the benchmarks
echo Starting collision system benchmarks...
echo Collision System Benchmarks - %date% %time% > "%RESULTS_FILE%"

if "%VERBOSE%"=="true" (
    echo Verbose mode enabled
    "%TEST_EXECUTABLE%" --log_level=all 2>&1 | powershell -Command "$input | Tee-Object -FilePath '%RESULTS_FILE%' -Append"
    set "RESULT=!ERRORLEVEL!"
) else (
    "%TEST_EXECUTABLE%" --log_level=test_suite 2>&1 | powershell -Command "$input | Tee-Object -FilePath '%RESULTS_FILE%' -Append"
    set "RESULT=!ERRORLEVEL!"
)

echo. >> "%RESULTS_FILE%"
echo Benchmark completed at: %date% %time% >> "%RESULTS_FILE%"
echo Exit code: !RESULT! >> "%RESULTS_FILE%"

REM Report results
echo.
echo ======================================================
if !RESULT! equ 0 (
    echo All collision system benchmarks completed successfully!
    echo.
    echo Benchmark Coverage:
    echo   CollisionManager SOA storage performance
    echo   SpatialHash insertion performance scaling
    echo   SpatialHash query performance with entity density
    echo   SpatialHash update performance during movement
    echo   Collision detection broadphase/narrowphase scaling
    echo   Threading performance comparison

    if exist "%CSV_FILE%" (
        echo.
        echo Detailed CSV results generated: %CSV_FILE%
        echo Import this file into spreadsheet software for analysis
    )
) else (
    echo Some benchmarks failed or encountered issues
    echo Check the detailed results in: %RESULTS_FILE%
)

echo.
echo Benchmark results saved to: %RESULTS_FILE%
echo ======================================================

REM Performance analysis and recommendations
if !RESULT! equ 0 (
    echo.
    echo Performance Analysis:
    echo Use the CSV file to identify:
    echo   * Optimal SpatialHash cell sizes for your entity density
    echo   * Entity count limits for maintaining target frame rates
    echo   * Threading benefits vs overhead for your use case
    echo   * Collision detection performance scaling characteristics
    echo.
    echo Recommended Performance Targets:
    echo   * SpatialHash insertion: ^< 50us per entity
    echo   * SpatialHash query: ^< 100us per query
    echo   * SpatialHash update: ^< 75us per update
    echo   * Collision detection: ^< 5ms for 1000 entities
    echo   * Frame budget: ^< 16.67ms total (60 FPS^)
)

exit /b !RESULT!
