@echo off
REM Script to run pathfinder system performance benchmarks on Windows
REM This script runs comprehensive pathfinding performance tests

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
    echo Pathfinder System Benchmark Runner
    echo Usage: run_pathfinder_benchmark.bat [options]
    echo.
    echo Options:
    echo   --verbose    Run benchmarks with verbose output
    echo   --help       Show this help message
    echo.
    echo Benchmark Coverage:
    echo   - Immediate pathfinding performance across grid sizes
    echo   - Async pathfinding request throughput and latency
    echo   - Cache performance and hit rate analysis
    echo   - Path length vs computation time scaling
    echo   - Threading overhead vs benefits analysis
    echo   - Obstacle density impact on performance
    echo.
    echo Estimated Runtime: 3-5 minutes
    goto :eof
)
echo Unknown option: %~1
echo Use --help for usage information
exit /b 1

:done_parsing

REM Create results directory
if not exist "%PROJECT_DIR%\test_results" mkdir "%PROJECT_DIR%\test_results"
set "RESULTS_FILE=%PROJECT_DIR%\test_results\pathfinder_benchmark_results.txt"
set "CSV_FILE=%PROJECT_DIR%\test_results\pathfinder_benchmark.csv"

echo ======================================================
echo            Pathfinder System Benchmarks
echo ======================================================
echo Note: This benchmark may take several minutes to complete

REM Check if the test executable exists
set "TEST_EXECUTABLE=%PROJECT_DIR%\bin\debug\pathfinder_benchmark.exe"

if not exist "%TEST_EXECUTABLE%" (
    echo Benchmark executable not found: %TEST_EXECUTABLE%
    echo Make sure you have built the project with tests enabled.
    echo Run: cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug ^&^& ninja -C build
    exit /b 1
)

REM Run the benchmarks
echo Starting pathfinder system benchmarks...
echo Pathfinder System Benchmarks - %date% %time% > "%RESULTS_FILE%"

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
    echo All pathfinder system benchmarks completed successfully!
    echo.
    echo Benchmark Coverage:
    echo   Immediate pathfinding performance across grid sizes
    echo   Async pathfinding throughput and latency analysis
    echo   Cache performance and hit rate optimization
    echo   Path length vs computation time scaling
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
    echo Use the results to identify:
    echo   * Optimal grid sizes for your target frame rates
    echo   * Threading benefits vs overhead for your workload
    echo   * Cache effectiveness for repeated pathfinding patterns
    echo   * Performance scaling with path length and complexity
    echo.
    echo Recommended Performance Targets:
    echo   * Immediate pathfinding: ^< 20ms per request
    echo   * Async throughput: ^> 100 paths/second
    echo   * Cache speedup: ^> 2x for repeated paths
    echo   * Success rate: ^> 90%% for reasonable requests
    echo   * Frame budget: ^< 16.67ms total (60 FPS^)
)

exit /b !RESULT!
