@echo off
:: Script to run the AI Scaling Benchmark
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
    echo AI Scaling Benchmark
    echo Usage: run_ai_benchmark.bat [--debug] [--release] [--verbose] [--help]
    exit /b 0
)
shift
goto :parse_args

:done_parsing

cd /d "%SCRIPT_DIR%" 2>nul

set "BENCHMARK_EXEC=%PROJECT_DIR%\bin\%BUILD_TYPE%\ai_scaling_benchmark.exe"

if not exist "!BENCHMARK_EXEC!" (
    echo Error: Benchmark executable not found: !BENCHMARK_EXEC!
    exit /b 1
)

if not exist "%PROJECT_DIR%\test_results" mkdir "%PROJECT_DIR%\test_results"
set "OUTPUT_FILE=%PROJECT_DIR%\test_results\ai_scaling_benchmark_output.txt"

echo Running AI Scaling Benchmark...

if "!VERBOSE!"=="true" (
    "!BENCHMARK_EXEC!" --log_level=all --catch_system_errors=no
) else (
    "!BENCHMARK_EXEC!" --log_level=message --catch_system_errors=no
)
set RESULT=!ERRORLEVEL!

if !RESULT! equ 0 (
    echo Benchmark completed successfully
    exit /b 0
) else (
    echo Benchmark failed with exit code !RESULT!
    exit /b !RESULT!
)
