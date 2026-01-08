@echo off
rem GameTimeManager System Test Runner
rem Copyright 2025 Hammer Forged Games

setlocal enabledelayedexpansion

rem Enable ANSI escape sequences (Windows 10+)
for /F %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"
rem Color codes for Windows
set "RED=%ESC%[91m"
set "GREEN=%ESC%[92m"
set "YELLOW=%ESC%[93m"
set "BLUE=%ESC%[94m"
set "NC=%ESC%[0m"

rem Process command line arguments
set VERBOSE=false
set RUN_ALL=true
set RUN_CORE=false
set RUN_CALENDAR=false
set RUN_SEASON=false

:parse_args
if "%~1"=="" goto done_parsing
if /i "%~1"=="--verbose" (
    set VERBOSE=true
    shift
    goto parse_args
)
if /i "%~1"=="--core" (
    set RUN_ALL=false
    set RUN_CORE=true
    shift
    goto parse_args
)
if /i "%~1"=="--calendar" (
    set RUN_ALL=false
    set RUN_CALENDAR=true
    shift
    goto parse_args
)
if /i "%~1"=="--season" (
    set RUN_ALL=false
    set RUN_SEASON=true
    shift
    goto parse_args
)
if /i "%~1"=="--help" (
    echo !BLUE!GameTimeManager System Test Runner!NC!
    echo Usage: run_game_time_tests.bat [options]
    echo.
    echo Options:
    echo   --verbose      Run tests with verbose output
    echo   --core         Run only core GameTimeManager tests
    echo   --calendar     Run only calendar tests
    echo   --season       Run only season tests
    echo   --help         Show this help message
    exit /b 0
)
shift
goto parse_args

:done_parsing

echo !BLUE!Running GameTimeManager System Tests!NC!

rem Track overall result
set OVERALL_RESULT=0

rem Run selected tests
if "%RUN_ALL%"=="true" (
    call :run_single_test game_time_manager_tests
    call :run_single_test game_time_manager_calendar_tests
    call :run_single_test game_time_manager_season_tests
) else (
    if "%RUN_CORE%"=="true" call :run_single_test game_time_manager_tests
    if "%RUN_CALENDAR%"=="true" call :run_single_test game_time_manager_calendar_tests
    if "%RUN_SEASON%"=="true" call :run_single_test game_time_manager_season_tests
)

goto show_summary

:run_single_test
set test_name=%~1

rem Check if test executable exists
set "TEST_EXECUTABLE=..\..\bin\debug\%test_name%.exe"
if not exist "!TEST_EXECUTABLE!" (
    set "TEST_EXECUTABLE=..\..\bin\debug\%test_name%"
    if not exist "!TEST_EXECUTABLE!" (
        echo !RED!Error: Test executable not found: %test_name%!NC!
        set OVERALL_RESULT=1
        goto :eof
    )
)

rem Set test options
set TEST_OPTS=--log_level=test_suite --catch_system_errors=no
if "%VERBOSE%"=="true" (
    set TEST_OPTS=--log_level=all --report_level=detailed
)

rem Create test results directory
if not exist "..\..\test_results" mkdir "..\..\test_results"

rem Run the test
echo Running %test_name%...
if "%VERBOSE%"=="true" (
    "!TEST_EXECUTABLE!" !TEST_OPTS!
) else (
    "!TEST_EXECUTABLE!" !TEST_OPTS! > "..\..\test_results\%test_name%_output.txt" 2>&1
)

set TEST_RESULT=!ERRORLEVEL!

if !TEST_RESULT! neq 0 (
    echo !RED!%test_name% failed with exit code !TEST_RESULT!!NC!
    set OVERALL_RESULT=1
) else (
    echo !GREEN!%test_name% completed successfully!NC!
)

goto :eof

:show_summary
if !OVERALL_RESULT! neq 0 (
    echo.
    echo !RED!Some GameTimeManager tests failed!!NC!
    exit /b 1
) else (
    echo.
    echo !GREEN!All GameTimeManager tests completed successfully!!NC!
    echo !GREEN!  GameTimeManager core tests!NC!
    echo !GREEN!  GameTimeManager calendar tests!NC!
    echo !GREEN!  GameTimeManager season tests!NC!
    exit /b 0
)
