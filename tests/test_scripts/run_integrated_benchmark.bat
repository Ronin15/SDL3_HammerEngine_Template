@echo off
REM Copyright (c) 2025 Hammer Forged Games
REM All rights reserved.
REM Licensed under the MIT License - see LICENSE file for details

REM Integrated System Load Benchmark Runner (Windows)
REM Tests all major managers under realistic combined load

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\.."
set "BIN_DIR=%PROJECT_ROOT%\bin\debug"
set "BENCHMARK_EXECUTABLE=%BIN_DIR%\integrated_system_benchmark.exe"

echo ============================================
echo   Integrated System Load Benchmark
echo ============================================
echo.

REM Check if executable exists
if not exist "%BENCHMARK_EXECUTABLE%" (
    echo Error: Benchmark executable not found at:
    echo   %BENCHMARK_EXECUTABLE%
    echo.
    echo Please build the tests first:
    echo   cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug
    echo   ninja -C build
    exit /b 1
)

REM Parse command line arguments
set VERBOSE=false
set SPECIFIC_TEST=

:parse_args
if "%~1"=="" goto run_benchmark
if /i "%~1"=="--verbose" (
    set VERBOSE=true
    shift
    goto parse_args
)
if /i "%~1"=="-v" (
    set VERBOSE=true
    shift
    goto parse_args
)
if /i "%~1"=="--test" (
    set SPECIFIC_TEST=%~2
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-t" (
    set SPECIFIC_TEST=%~2
    shift
    shift
    goto parse_args
)
if /i "%~1"=="--help" goto show_help
if /i "%~1"=="-h" goto show_help

echo Unknown option: %~1
echo Use --help for usage information
exit /b 1

:show_help
echo Usage: %~nx0 [OPTIONS]
echo.
echo Options:
echo   --verbose, -v          Show detailed output
echo   --test ^<name^>, -t      Run specific test case
echo   --help, -h             Show this help message
echo.
echo Available test cases:
echo   TestRealisticGameSimulation60FPS
echo   TestScalingUnderLoad
echo   TestManagerCoordinationOverhead
echo   TestSustainedPerformance
exit /b 0

:run_benchmark
REM Build command options
set "TEST_OPTS="

if defined SPECIFIC_TEST (
    echo Running specific test: %SPECIFIC_TEST%
    set "TEST_OPTS=--run_test=IntegratedSystemBenchmarkSuite/%SPECIFIC_TEST%"
) else (
    echo Running all integrated system benchmarks
    set "TEST_OPTS=--run_test=IntegratedSystemBenchmarkSuite"
)

if "%VERBOSE%"=="true" (
    set "TEST_OPTS=!TEST_OPTS! --log_level=all"
) else (
    set "TEST_OPTS=!TEST_OPTS! --log_level=test_suite"
)

echo.
echo Command: "%BENCHMARK_EXECUTABLE%" !TEST_OPTS!
echo.

REM Run the benchmark
cd /d "%PROJECT_ROOT%"
"%BENCHMARK_EXECUTABLE%" !TEST_OPTS!
set BENCHMARK_RESULT=!ERRORLEVEL!

if !BENCHMARK_RESULT! EQU 0 (
    echo.
    echo ============================================
    echo   Benchmark completed successfully
    echo ============================================
    exit /b 0
) else (
    echo.
    echo ============================================
    echo   Benchmark failed with exit code !BENCHMARK_RESULT!
    echo ============================================
    exit /b !BENCHMARK_RESULT!
)
