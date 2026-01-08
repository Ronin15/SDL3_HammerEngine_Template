@echo off
:: EventManager Scaling Benchmark Test Script
:: Copyright (c) 2025 Hammer Forged Games
:: Licensed under the MIT License

setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%..\.."
set "BUILD_TYPE=debug"
set "VERBOSE=false"

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--debug" set "BUILD_TYPE=debug"& shift& goto :parse_args
if /i "%~1"=="--release" set "BUILD_TYPE=release"& shift& goto :parse_args
if /i "%~1"=="--verbose" set "VERBOSE=true"& shift& goto :parse_args
if /i "%~1"=="--help" (
    echo EventManager Scaling Benchmark
    echo Usage: run_event_scaling_benchmark.bat [--debug] [--release] [--verbose] [--help]
    exit /b 0
)
shift
goto :parse_args

:done_parsing

cd /d "%SCRIPT_DIR%" 2>nul

set "BENCHMARK_EXEC=%PROJECT_DIR%\bin\%BUILD_TYPE%\event_manager_scaling_benchmark.exe"

if not exist "!BENCHMARK_EXEC!" (
    echo Error: Benchmark executable not found: !BENCHMARK_EXEC!
    exit /b 1
)

if not exist "%PROJECT_DIR%\test_results" mkdir "%PROJECT_DIR%\test_results"
set "OUTPUT_FILE=%PROJECT_DIR%\test_results\event_scaling_benchmark_output.txt"

echo ======================================================
echo        EventManager Scaling Benchmark
echo ======================================================
echo Running event scaling benchmarks...

if "!VERBOSE!"=="true" (
    "!BENCHMARK_EXEC!" --log_level=all
    set RESULT=!ERRORLEVEL!
) else (
    "!BENCHMARK_EXEC!" --log_level=test_suite > "!OUTPUT_FILE!" 2>&1
    set RESULT=!ERRORLEVEL!
)

echo.
echo ======================================================
if !RESULT! equ 0 (
    echo Benchmark completed successfully!
    echo.
    echo Benchmark Coverage:
    echo   [OK] Basic handler performance
    echo   [OK] Medium scale ^(5K events, 25K handlers^)
    echo   [OK] Scalability suite ^(WorkerBudget^)
    echo   [OK] Concurrency testing ^(multi-threaded^)
    echo   [OK] Extreme scale testing
    echo.
    echo Performance Targets:
    echo   * WorkerBudget: 30%% worker allocation
    echo   * Events per second: ^> 100K
    echo   * Handler throughput: ^> 1M calls/second
) else (
    echo Benchmark failed with exit code !RESULT!
    echo Check the detailed results in: !OUTPUT_FILE!
)
echo.
echo Results saved to: !OUTPUT_FILE!
echo ======================================================

exit /b !RESULT!
