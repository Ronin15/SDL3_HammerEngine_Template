@echo off
:: Script to run Adaptive Threading Analysis benchmarks
:: Measures single vs multi-threaded throughput to determine optimal threading thresholds

setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%..\.."
set "VERBOSE=false"

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--verbose" set "VERBOSE=true"& shift& goto :parse_args
if /i "%~1"=="--help" (
    echo Adaptive Threading Analysis
    echo Usage: run_adaptive_threading_analysis.bat [--verbose] [--help]
    echo.
    echo Options:
    echo   --verbose    Run benchmarks with verbose output
    echo   --help       Show this help message
    echo.
    echo Analysis Coverage:
    echo   - AI Threading Crossover: Find optimal entity count for multi-threading
    echo   - Collision Threading Crossover: Find optimal entity count for collision
    echo   - AI Adaptive Learning: 3000-frame throughput tracking test
    echo   - Collision Adaptive Learning: 3000-frame throughput tracking test
    echo   - Summary: Per-system throughput comparison
    echo.
    echo Estimated Runtime: 5-8 minutes
    exit /b 0
)
shift
goto :parse_args

:done_parsing

cd /d "%SCRIPT_DIR%" 2>nul

set "BENCHMARK_EXEC=%PROJECT_DIR%\bin\debug\adaptive_threading_analysis.exe"

if not exist "!BENCHMARK_EXEC!" (
    echo Error: Analysis executable not found: !BENCHMARK_EXEC!
    echo Make sure you have built the project with tests enabled.
    echo Run: cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug ^&^& ninja -C build
    exit /b 1
)

if not exist "%PROJECT_DIR%\test_results" mkdir "%PROJECT_DIR%\test_results"
set "OUTPUT_FILE=%PROJECT_DIR%\test_results\adaptive_threading_analysis_output.txt"

echo ======================================================
echo          Adaptive Threading Analysis
echo ======================================================
echo Finding optimal threading thresholds per system
echo Running adaptive threading analysis...

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
    echo All adaptive threading analyses completed successfully!
    echo.
    echo Analysis Coverage:
    echo   [OK] AI Threading Crossover
    echo   [OK] Collision Threading Crossover
    echo   [OK] AI Adaptive Learning ^(3000 frames^)
    echo   [OK] Collision Adaptive Learning ^(3000 frames^)
    echo   [OK] Per-System Summary
    echo.
    echo Key Metrics Measured:
    echo   * Single-threaded throughput ^(items/ms^)
    echo   * Multi-threaded throughput ^(items/ms^)
    echo   * Speedup ratio ^(multi/single^)
    echo   * Crossover point ^(where speedup ^> 1.1x^)
    echo   * Batch multiplier adaptation
) else (
    echo Some analyses failed or encountered issues
    echo Check the detailed results in: !OUTPUT_FILE!
)
echo.
echo Results saved to: !OUTPUT_FILE!
echo ======================================================

exit /b !RESULT!
