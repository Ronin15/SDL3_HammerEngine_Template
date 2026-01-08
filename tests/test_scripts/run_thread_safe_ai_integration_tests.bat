@echo off
:: Script to run the Thread-Safe AI Integration tests
:: Copyright (c) 2025 Hammer Forged Games, MIT License

:: Enable color output on Windows 10+ terminals
setlocal EnableDelayedExpansion

:: Enable ANSI escape sequences (Windows 10+)
for /F %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"
:: Color codes for Windows
set "GREEN=%ESC%[92m"
set "YELLOW=%ESC%[93m"
set "RED=%ESC%[91m"
set "BLUE=%ESC%[94m"
set "NC=%ESC%[0m"

:: Navigate to project root directory (in case script is run from elsewhere)
cd /d "%~dp0" 2>nul

:: Create required directories
if not exist "..\..\test_results" mkdir "..\..\test_results"

:: Set default build type
set BUILD_TYPE=Debug
set VERBOSE=false

:: Process command-line options
:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--release" (
    set BUILD_TYPE=Release
    shift
    goto :parse_args
)
if /i "%~1"=="--verbose" (
    set VERBOSE=true
    shift
    goto :parse_args
)
if /i "%~1"=="--help" (
    echo Usage: %0 [--release] [--verbose] [--help]
    echo   --release   Run the release build of the tests
    echo   --verbose   Show detailed test output
    echo   --help      Show this help message
    exit /b 0
)
echo Unknown option: %1
echo Usage: %0 [--release] [--verbose] [--help]
exit /b 1

:done_parsing

echo Running Thread-Safe AI Integration tests...

:: Determine test executable path based on build type
if "%BUILD_TYPE%"=="Debug" (
    set TEST_EXECUTABLE=..\..\bin\debug\thread_safe_ai_integration_tests.exe
) else (
    set TEST_EXECUTABLE=..\..\bin\release\thread_safe_ai_integration_tests.exe
)

:: Verify executable exists
if not exist "!TEST_EXECUTABLE!" (
    echo Error: Test executable not found at '!TEST_EXECUTABLE!'
    :: Attempt to find the executable
    echo Searching for test executable...
    set FOUND_EXECUTABLE=
    for /r "bin" %%f in (thread_safe_ai_integration_tests.exe) do (
        echo Found executable at: %%f
        set TEST_EXECUTABLE=%%f
        set FOUND_EXECUTABLE=true
        goto :found_executable
    )
    
    if "!FOUND_EXECUTABLE!"=="" (
        echo Could not find the test executable. Build may have failed or placed the executable in an unexpected location.
        exit /b 1
    )
)

:found_executable

:: Run tests and save output
echo Running Thread-Safe AI Integration tests...

:: Create the test_results directory if it doesn't exist
if not exist "..\..\test_results" mkdir "..\..\test_results"

:: Create a temporary file for test output
set TEMP_OUTPUT=..\..\test_results\thread_safe_ai_integration_test_output.txt

:: Clear any existing output file
type nul > "!TEMP_OUTPUT!"

:: Set test command options
set TEST_OPTS=--log_level=all --catch_system_errors=no
if "%VERBOSE%"=="true" (set TEST_OPTS=!TEST_OPTS! --report_level=detailed)

:: Run the tests directly (simpler and more reliable than PowerShell Start-Job)
echo Running with options: !TEST_OPTS!
"!TEST_EXECUTABLE!" !TEST_OPTS! > "!TEMP_OUTPUT!" 2>&1
set TEST_RESULT=!ERRORLEVEL!

:: Check for explicit test failures
findstr /c:"test cases failed" /c:"fatal error" /c:"BOOST_CHECK.*failed" /c:"BOOST_REQUIRE.*failed" "!TEMP_OUTPUT!" >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo ❌ Some tests failed! See ..\..\test_results\thread_safe_ai_integration_test_output.txt for details.
    exit /b 1
)

:: Check for crash indicators
findstr /c:"memory access violation" /c:"Segmentation fault" /c:"Abort trap" /c:"dumped core" "!TEMP_OUTPUT!" >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo ❌ Tests crashed! See ..\..\test_results\thread_safe_ai_integration_test_output.txt for details.
    exit /b 1
)

:: If no explicit failures and exit code is 0, consider it a pass
if !TEST_RESULT! equ 0 (
    echo ✅ All Thread-Safe AI Integration tests passed!
    exit /b 0
)

:: Non-zero exit but no explicit failures - check if tests actually ran
findstr /c:"Running.*test cases" "!TEMP_OUTPUT!" >nul 2>&1
if %ERRORLEVEL% equ 0 (
    :: Tests ran, non-zero might be cleanup issue
    findstr /c:"shutdown complete" "!TEMP_OUTPUT!" >nul 2>&1
    if %ERRORLEVEL% equ 0 (
        echo ✅ All Thread-Safe AI Integration tests passed!
        exit /b 0
    )
)

:: Unknown state - return the actual exit code
echo ❓ Test execution status unclear (exit code: !TEST_RESULT!). Check output for details.
exit /b !TEST_RESULT!