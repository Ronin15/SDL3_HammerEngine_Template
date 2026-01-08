@echo off
:: Script to run the WorldManager event integration tests

setlocal EnableDelayedExpansion

:: Enable ANSI escape sequences (Windows 10+)
for /F %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"
:: Set up colored output
set "RED=%ESC%[91m"
set "GREEN=%ESC%[92m"
set "YELLOW=%ESC%[93m"
set "BLUE=%ESC%[94m"
set "NC=%ESC%[0m"

:: Navigate to script directory
cd /d "%~dp0" 2>nul

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
    echo !BLUE!WorldManager Event Integration Test Runner!NC!
    echo Usage: run_world_manager_event_integration_tests.bat [options]
    echo.
    echo Options:
    echo   --verbose    Run tests with verbose output
    echo   --help       Show this help message
    exit /b 0
)
shift
goto :parse_args

:done_parsing

echo !BLUE!Running WorldManager event integration tests...!NC!

:: Locate test executable
set "TEST_EXECUTABLE=..\..\bin\debug\world_manager_event_integration_tests.exe"
if not exist "!TEST_EXECUTABLE!" (
    set "TEST_EXECUTABLE=..\..\bin\debug\world_manager_event_integration_tests"
    if not exist "!TEST_EXECUTABLE!" (
        echo !RED!Test executable not found: !TEST_EXECUTABLE!!NC!
        echo !YELLOW!Searching for test executable...!NC!
        set "FOUND_EXECUTABLE="
        for /r ..\.. %%f in (world_manager_event_integration_tests.exe world_manager_event_integration_tests) do (
            if exist "%%f" (
                set "TEST_EXECUTABLE=%%f"
                set "FOUND_EXECUTABLE=true"
                echo !GREEN!Found test executable at %%f!NC!
                goto :found_executable
            )
        )
        if "!FOUND_EXECUTABLE!"=="" (
            echo !RED!Please build the tests first.!NC!
            exit /b 1
        )
    )
)

:found_executable

:: Create test_results directory if it doesn't exist
if not exist "..\..\test_results" mkdir "..\..\test_results"

:: Set up test environment
set SDL_VIDEODRIVER=dummy

:: Run the test
echo !GREEN!Running tests...!NC!
if "%VERBOSE%"=="true" (
    "!TEST_EXECUTABLE!" --log_level=all --report_level=detailed
) else (
    "!TEST_EXECUTABLE!" --log_level=error --report_level=short
)

set TEST_RESULT=!ERRORLEVEL!

if !TEST_RESULT! equ 0 (
    echo !GREEN!✓ WorldManager event integration tests passed!!NC!
) else (
    echo !RED!✗ WorldManager event integration tests failed with exit code !TEST_RESULT!!NC!
)

exit /b !TEST_RESULT!