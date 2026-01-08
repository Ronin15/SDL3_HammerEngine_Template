@echo off
REM Script to run Particle Manager performance benchmarks on Windows
REM Copyright (c) 2025 Hammer Forged Games, MIT License

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\.."
set "VERBOSE=false"
set "BUILD_TYPE=debug"

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--verbose" set "VERBOSE=true"& shift& goto :parse_args
if /i "%~1"=="--debug" set "BUILD_TYPE=debug"& shift& goto :parse_args
if /i "%~1"=="--release" set "BUILD_TYPE=release"& shift& goto :parse_args
if /i "%~1"=="--help" (
    echo Particle Manager Performance Benchmark
    echo Usage: run_particle_manager_benchmark.bat [--verbose] [--debug] [--release] [--help]
    exit /b 0
)
shift
goto :parse_args

:done_parsing

cd /d "%SCRIPT_DIR%" 2>nul

set "BENCHMARK_EXEC=%PROJECT_ROOT%\bin\%BUILD_TYPE%\particle_manager_performance_tests.exe"

if not exist "!BENCHMARK_EXEC!" (
    echo Error: Benchmark executable not found: !BENCHMARK_EXEC!
    exit /b 1
)

if not exist "%PROJECT_ROOT%\test_results" mkdir "%PROJECT_ROOT%\test_results"
set "OUTPUT_FILE=%PROJECT_ROOT%\test_results\particle_manager_benchmark_output.txt"

echo ======================================================
echo      Particle Manager Performance Benchmark
echo ======================================================
echo Running particle manager benchmarks...

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
    echo   [OK] Update Performance ^(1K particles^)
    echo   [OK] Update Performance ^(5K particles^)
    echo   [OK] Sustained Performance ^(60 frames^)
    echo   [OK] High Count Benchmarks ^(10K/25K/50K^)
    echo.
    echo Performance Targets:
    echo   * 1K particles update: ^< 1ms
    echo   * 5K particles update: ^< 5ms
    echo   * Sustained 60fps with 1500 particles
) else (
    echo Benchmark failed with exit code !RESULT!
    echo Check the detailed results in: !OUTPUT_FILE!
)
echo.
echo Results saved to: !OUTPUT_FILE!
echo ======================================================

exit /b !RESULT!
