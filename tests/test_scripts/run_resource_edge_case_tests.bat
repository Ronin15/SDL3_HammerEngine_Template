@echo off
:: Resource Edge Case Test Runner
:: Runs comprehensive edge case tests for the resource system

setlocal EnableDelayedExpansion

:: Set up colored output
set "RED=[91m"
set "GREEN=[92m"
set "YELLOW=[93m"
set "BLUE=[94m"
set "NC=[0m"

:: Navigate to script directory
cd /d "%~dp0"

:: Process command line arguments
set VERBOSE=false

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--verbose" (
    set VERBOSE=true
    shift
    goto :parse_args
)
if /i "%~1"=="--help" (
    echo !BLUE!Resource Edge Case Test Runner!NC!
    echo Usage: run_resource_edge_case_tests.bat [options]
    echo.
    echo Options:
    echo   --verbose    Run tests with verbose output
    echo   --help       Show this help message
    echo.
    echo Test Coverage:
    echo   - Handle lifecycle edge cases ^(overflow, stale handles^)
    echo   - Concurrent access patterns and race conditions  
    echo   - Memory pressure and resource exhaustion
    echo   - Malformed input and error recovery
    echo   - Performance under extreme load conditions
    echo   - System integration edge cases
    exit /b 0
)
shift
goto :parse_args

:done_parsing

echo !BLUE!Running Resource System Edge Case Tests...!NC!
echo !BLUE!==========================================!NC!

:: Locate test executable
set "TEST_EXECUTABLE=..\..\bin\debug\resource_edge_case_tests.exe"
if not exist "!TEST_EXECUTABLE!" (
    set "TEST_EXECUTABLE=..\..\bin\debug\resource_edge_case_tests"
    if not exist "!TEST_EXECUTABLE!" (
        echo !RED!ERROR: Test binary not found at !TEST_EXECUTABLE!!NC!
        echo !YELLOW!Searching for test executable...!NC!
        set "FOUND_EXECUTABLE="
        for /r ..\.. %%f in (resource_edge_case_tests.exe resource_edge_case_tests) do (
            if exist "%%f" (
                set "TEST_EXECUTABLE=%%f"
                set "FOUND_EXECUTABLE=true"
                echo !GREEN!Found test executable at %%f!NC!
                goto :found_executable
            )
        )
        if "!FOUND_EXECUTABLE!"=="" (
            echo !RED!Could not find test executable!!NC!
            echo !YELLOW!Please build the tests first with: ninja -C build resource_edge_case_tests!NC!
            exit /b 1
        )
    )
)

:found_executable

:: Create test_results directory if it doesn't exist
if not exist "..\..\test_results" mkdir "..\..\test_results"

:: Set up test environment
set SDL_VIDEODRIVER=dummy

echo !GREEN!Executing edge case tests with 60 second timeout...!NC!

:: Run tests with timeout to prevent hanging on concurrent tests
if "%VERBOSE%"=="true" (
    timeout 60 "!TEST_EXECUTABLE!" --log_level=all --report_level=detailed
) else (
    timeout 60 "!TEST_EXECUTABLE!" --log_level=error --report_level=short
)

set TEST_RESULT=!ERRORLEVEL!

if !TEST_RESULT! equ 0 (
    echo.
    echo !GREEN!✅ All edge case tests PASSED!!NC!
    echo.
    echo !BLUE!Resource system successfully validated against:!NC!
    echo !CYAN!• Handle lifecycle edge cases ^(overflow, stale handles^)!NC!
    echo !CYAN!• Concurrent access patterns and race conditions!NC!
    echo !CYAN!• Memory pressure and resource exhaustion!NC!
    echo !CYAN!• Malformed input and error recovery!NC!
    echo !CYAN!• Performance under extreme load conditions!NC!
    echo !CYAN!• System integration edge cases!NC!
) else if !TEST_RESULT! equ 1 (
    echo.
    echo !YELLOW!⚠️  Tests TIMED OUT after 60 seconds!NC!
    echo !YELLOW!This may indicate performance issues in concurrent tests!NC!
) else (
    echo.
    echo !RED!❌ Some tests FAILED ^(exit code: !TEST_RESULT!^)!NC!
    echo !YELLOW!Check the output above for details!NC!
)

exit /b !TEST_RESULT!