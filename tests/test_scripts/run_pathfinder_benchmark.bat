@echo off
REM Script to run pathfinder system performance benchmarks on Windows

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%..\.."
set "VERBOSE=false"

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--verbose" set "VERBOSE=true"& shift& goto :parse_args
if /i "%~1"=="--help" (
    echo Pathfinder System Benchmark
    echo Usage: run_pathfinder_benchmark.bat [--verbose] [--help]
    exit /b 0
)
shift
goto :parse_args

:done_parsing

cd /d "%SCRIPT_DIR%" 2>nul

set "BENCHMARK_EXEC=%PROJECT_DIR%\bin\debug\pathfinder_benchmark.exe"

if not exist "!BENCHMARK_EXEC!" (
    echo Error: Benchmark executable not found: !BENCHMARK_EXEC!
    exit /b 1
)

if not exist "%PROJECT_DIR%\test_results" mkdir "%PROJECT_DIR%\test_results"
set "OUTPUT_FILE=%PROJECT_DIR%\test_results\pathfinder_benchmark_output.txt"

echo ======================================================
echo           Pathfinder System Benchmark
echo ======================================================
echo Running pathfinder benchmarks...

if "!VERBOSE!"=="true" (
    "!BENCHMARK_EXEC!" --log_level=all 2>&1 | powershell -Command "$input | Tee-Object -FilePath '!OUTPUT_FILE!'"
    set RESULT=!ERRORLEVEL!
) else (
    "!BENCHMARK_EXEC!" --log_level=test_suite 2>&1 | powershell -Command "$input | Tee-Object -FilePath '!OUTPUT_FILE!'" > nul
    set RESULT=!ERRORLEVEL!
)

echo.
echo ======================================================
if !RESULT! equ 0 (
    echo Benchmark completed successfully!
    echo.
    echo Benchmark Coverage:
    echo   [OK] Immediate pathfinding performance
    echo   [OK] Async pathfinding throughput
    echo   [OK] Cache performance and hit rates
    echo   [OK] Path length scaling
    echo   [OK] Threading performance
    echo.
    echo Performance Targets:
    echo   * Immediate pathfinding: ^< 20ms per request
    echo   * Async throughput: ^> 100 paths/second
    echo   * Cache speedup: ^> 2x for repeated paths
) else (
    echo Benchmark failed with exit code !RESULT!
    echo Check the detailed results in: !OUTPUT_FILE!
)
echo.
echo Results saved to: !OUTPUT_FILE!
echo ======================================================

exit /b !RESULT!
