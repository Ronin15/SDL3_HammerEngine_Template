@echo off
:: Script to run the AI and Collision Integration Tests
:: Copyright (c) 2025 Hammer Forged Games, MIT License

setlocal EnableDelayedExpansion

:: Navigate to script directory first
cd /d "%~dp0" 2>nul

:: Enable ANSI escape sequences (Windows 10+)
for /F %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"
set "GREEN=%ESC%[92m"
set "YELLOW=%ESC%[93m"
set "RED=%ESC%[91m"
set "NC=%ESC%[0m"

echo !YELLOW!Running AI and Collision Integration Tests...!NC!

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
    set TEST_EXECUTABLE=..\..\bin\debug\ai_collision_integration_tests.exe
) else (
    set TEST_EXECUTABLE=..\..\bin\release\ai_collision_integration_tests.exe
)

if not exist "!TEST_EXECUTABLE!" (
    echo !RED!Error: Test executable not found at '!TEST_EXECUTABLE!'!NC!
    exit /b 1
)

set OUTPUT_FILE=..\..\test_results\ai_collision_integration_tests_output.txt
set TEST_OPTS=--log_level=all --catch_system_errors=no
if "%VERBOSE%"=="true" (set TEST_OPTS=!TEST_OPTS! --report_level=detailed)

"!TEST_EXECUTABLE!" !TEST_OPTS! > "!OUTPUT_FILE!" 2>&1
set TEST_RESULT=!ERRORLEVEL!

:: Check for success indicator first
findstr /c:"No errors detected" "!OUTPUT_FILE!" >nul 2>&1
if !ERRORLEVEL! equ 0 (
    echo !GREEN!All AI and Collision Integration tests passed!!NC!
    exit /b 0
)

:: Check for explicit failures
findstr /c:"failure" /c:"test cases failed" /c:"fatal error" "!OUTPUT_FILE!" >nul 2>&1
if !ERRORLEVEL! equ 0 (
    echo !RED!Some tests failed! See !OUTPUT_FILE! for details.!NC!
    exit /b 1
)

:: Fall back to exit code
if !TEST_RESULT! neq 0 (
    echo !RED!Tests failed with exit code !TEST_RESULT!!NC!
    exit /b 1
)

echo !GREEN!All AI and Collision Integration tests passed!!NC!
exit /b 0
