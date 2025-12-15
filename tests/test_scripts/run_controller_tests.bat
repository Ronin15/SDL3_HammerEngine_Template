@echo off
rem World Controller Test Runner (Time, Weather, DayNight)
rem Copyright 2025 Hammer Forged Games

setlocal enabledelayedexpansion

rem Color codes for Windows
set "RED=[91m"
set "GREEN=[92m"
set "YELLOW=[93m"
set "BLUE=[94m"
set "NC=[0m"

rem Process command line arguments
set VERBOSE=false
set RUN_ALL=true
set RUN_TIME=false
set RUN_WEATHER=false
set RUN_DAYNIGHT=false

:parse_args
if "%~1"=="" goto done_parsing
if /i "%~1"=="--verbose" (
    set VERBOSE=true
    shift
    goto parse_args
)
if /i "%~1"=="--time" (
    set RUN_ALL=false
    set RUN_TIME=true
    shift
    goto parse_args
)
if /i "%~1"=="--weather" (
    set RUN_ALL=false
    set RUN_WEATHER=true
    shift
    goto parse_args
)
if /i "%~1"=="--daynight" (
    set RUN_ALL=false
    set RUN_DAYNIGHT=true
    shift
    goto parse_args
)
if /i "%~1"=="--help" (
    echo !BLUE!World Controller Test Runner!NC!
    echo Usage: run_controller_tests.bat [options]
    echo.
    echo Options:
    echo   --verbose      Run tests with verbose output
    echo   --time         Run only TimeController tests
    echo   --weather      Run only WeatherController tests
    echo   --daynight     Run only DayNightController tests
    echo   --help         Show this help message
    exit /b 0
)
shift
goto parse_args

:done_parsing

echo !BLUE!Running World Controller Tests!NC!

rem Track overall result
set OVERALL_RESULT=0

rem Run selected tests
if "%RUN_ALL%"=="true" (
    call :run_single_test time_controller_tests
    call :run_single_test weather_controller_tests
    call :run_single_test day_night_controller_tests
) else (
    if "%RUN_TIME%"=="true" call :run_single_test time_controller_tests
    if "%RUN_WEATHER%"=="true" call :run_single_test weather_controller_tests
    if "%RUN_DAYNIGHT%"=="true" call :run_single_test day_night_controller_tests
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
    echo !RED!Some controller tests failed!!NC!
    exit /b 1
) else (
    echo.
    echo !GREEN!All controller tests completed successfully!!NC!
    echo !GREEN!  TimeController tests!NC!
    echo !GREEN!  WeatherController tests!NC!
    echo !GREEN!  DayNightController tests!NC!
    exit /b 0
)
