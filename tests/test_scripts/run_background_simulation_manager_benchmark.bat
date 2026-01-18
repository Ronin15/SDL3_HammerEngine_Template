@echo off
:: Script to run BackgroundSimulationManager benchmarks
:: Tests background entity scaling, threading threshold detection, and WorkerBudget adaptive tuning

setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%..\.."
set "VERBOSE=false"

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--verbose" set "VERBOSE=true"& shift& goto :parse_args
if /i "%~1"=="--help" (
    echo BackgroundSimulationManager Benchmark
    echo Usage: run_background_simulation_manager_benchmark.bat [--verbose] [--help]
    echo.
    echo Options:
    echo   --verbose    Run benchmarks with verbose output
    echo   --help       Show this help message
    echo.
    echo Benchmark Coverage:
    echo   - Entity Scaling: 100 to 10,000 background entities
    echo   - Threading Threshold: Single vs multi-threaded crossover detection
    echo   - Adaptive Tuning: WorkerBudget batch sizing and throughput tracking
    echo.
    echo Estimated Runtime: 3-5 minutes
    exit /b 0
)
shift
goto :parse_args

:done_parsing

cd /d "%SCRIPT_DIR%" 2>nul

set "BENCHMARK_EXEC=%PROJECT_DIR%\bin\debug\background_simulation_manager_benchmark.exe"

if not exist "!BENCHMARK_EXEC!" (
    echo Error: Benchmark executable not found: !BENCHMARK_EXEC!
    echo Make sure you have built the project with tests enabled.
    echo Run: cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug ^&^& ninja -C build
    exit /b 1
)

if not exist "%PROJECT_DIR%\test_results" mkdir "%PROJECT_DIR%\test_results"
set "OUTPUT_FILE=%PROJECT_DIR%\test_results\background_simulation_benchmark_output.txt"

echo ======================================================
echo      BackgroundSimulationManager Benchmark
echo ======================================================
echo Testing background entity processing and tier-based simulation
echo Running BackgroundSimulationManager benchmarks...

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
    echo All BackgroundSimulationManager benchmarks completed successfully!
    echo.
    echo Benchmark Coverage:
    echo   [OK] Entity Scaling ^(100-10K entities^)
    echo   [OK] Threading Threshold Detection
    echo   [OK] WorkerBudget Adaptive Tuning
    echo.
    echo Performance Targets:
    echo   * 1000 background entities: ^< 1ms per update
    echo   * 5000 background entities: ^< 3ms per update
    echo   * 10000 background entities: ^< 6ms per update
    echo   * Batch sizing convergence verified
    echo   * Throughput tracking active
) else (
    echo Some benchmarks failed or encountered issues
    echo Check the detailed results in: !OUTPUT_FILE!
)
echo.
echo Results saved to: !OUTPUT_FILE!
echo ======================================================

exit /b !RESULT!
