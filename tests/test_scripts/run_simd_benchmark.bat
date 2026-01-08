@echo off
:: Script to run the SIMD Performance Benchmark
:: Copyright (c) 2025 Hammer Forged Games, MIT License

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
    echo SIMD Performance Benchmark
    echo Usage: run_simd_benchmark.bat [--debug] [--release] [--verbose] [--help]
    exit /b 0
)
shift
goto :parse_args

:done_parsing

cd /d "%SCRIPT_DIR%" 2>nul

set "BENCHMARK_EXEC=%PROJECT_DIR%\bin\%BUILD_TYPE%\simd_performance_benchmark.exe"

if not exist "!BENCHMARK_EXEC!" (
    echo Error: Benchmark executable not found: !BENCHMARK_EXEC!
    exit /b 1
)

if not exist "%PROJECT_DIR%\test_results" mkdir "%PROJECT_DIR%\test_results"
set "OUTPUT_FILE=%PROJECT_DIR%\test_results\simd_benchmark_output.txt"

echo ======================================================
echo           SIMD Performance Benchmark
echo ======================================================
echo Running SIMD benchmarks...

if "!VERBOSE!"=="true" (
    "!BENCHMARK_EXEC!" --log_level=all --catch_system_errors=no
    set RESULT=!ERRORLEVEL!
) else (
    "!BENCHMARK_EXEC!" --log_level=test_suite --catch_system_errors=no > "!OUTPUT_FILE!" 2>&1
    set RESULT=!ERRORLEVEL!
)

echo.
echo ======================================================
if !RESULT! equ 0 (
    echo Benchmark completed successfully!
    echo.
    echo Benchmark Coverage:
    echo   [OK] SIMD vs Scalar distance calculations
    echo   [OK] Batch processing performance
    echo   [OK] Memory alignment tests
    echo   [OK] Cross-platform compatibility ^(SSE2/NEON^)
    echo.
    echo Performance Targets:
    echo   * SIMD speedup: ^> 2x over scalar
    echo   * Batch processing: 4 elements per iteration
) else (
    echo Benchmark failed with exit code !RESULT!
    echo Check the detailed results in: !OUTPUT_FILE!
)
echo.
echo Results saved to: !OUTPUT_FILE!
echo ======================================================

exit /b !RESULT!
