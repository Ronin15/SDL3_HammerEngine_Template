@echo off
:: Script to run the SIMD Performance Benchmark
:: Copyright (c) 2025 Hammer Forged Games, MIT License

setlocal EnableDelayedExpansion

set "GREEN=[92m"
set "YELLOW=[93m"
set "RED=[91m"
set "NC=[0m"

echo !YELLOW!Running SIMD Performance Benchmark...!NC!

cd /d "%~dp0"

if not exist "..\..\test_results" mkdir "..\..\test_results"

set BUILD_TYPE=Debug
set VERBOSE=false

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--debug" (set BUILD_TYPE=Debug& shift& goto :parse_args)
if /i "%~1"=="--release" (set BUILD_TYPE=Release& shift& goto :parse_args)
if /i "%~1"=="--verbose" (set VERBOSE=true& shift& goto :parse_args)
if /i "%~1"=="--help" (echo Usage: %0 [--debug] [--release] [--verbose] [--help]& exit /b 0)
echo Unknown option: %1& exit /b 1

:done_parsing

if "%BUILD_TYPE%"=="Debug" (
    set TEST_EXECUTABLE=..\..\bin\debug\simd_performance_benchmark.exe
) else (
    set TEST_EXECUTABLE=..\..\bin\release\simd_performance_benchmark.exe
)

if not exist "!TEST_EXECUTABLE!" (
    echo !RED!Error: Test executable not found at '!TEST_EXECUTABLE!'!NC!
    exit /b 1
)

set OUTPUT_FILE=..\..\test_results\simd_performance_benchmark_output.txt
set TEST_OPTS=--log_level=all --catch_system_errors=no
if "%VERBOSE%"=="true" (set TEST_OPTS=!TEST_OPTS! --report_level=detailed)

"!TEST_EXECUTABLE!" !TEST_OPTS! > "!OUTPUT_FILE!" 2>&1
set TEST_RESULT=%ERRORLEVEL%

findstr /c:"failure" /c:"test cases failed" /c:"fatal error" "!OUTPUT_FILE!" >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo !RED!❌ Some tests failed! See !OUTPUT_FILE! for details.!NC!
    exit /b 1
) else (
    if !TEST_RESULT! neq 0 (
        echo !RED!❌ Some tests failed! See !OUTPUT_FILE! for details.!NC!
        exit /b 1
    ) else (
        echo !GREEN!✅ All SIMD Performance Benchmark tests passed!!NC!
        exit /b 0
    )
)
