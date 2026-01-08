@echo off
:: Helper script to run ThreadSystem tests

:: Enable color output on Windows 10+ terminals
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
    echo !BLUE!ThreadSystem Test Runner!NC!
    echo Usage: run_thread_tests.bat [options]
    echo.
    echo Options:
    echo   --verbose    Run tests with verbose output
    echo   --help       Show this help message
    exit /b 0
)
shift
goto :parse_args

:done_parsing

echo !BLUE!Running ThreadSystem tests...!NC!

:: Check if test executable exists
set TEST_EXECUTABLE=..\..\bin\debug\thread_system_tests.exe
if not exist "!TEST_EXECUTABLE!" (
    set TEST_EXECUTABLE=..\..\bin\debug\thread_system_tests
    if not exist "!TEST_EXECUTABLE!" (
        echo !RED!Test executable not found at !TEST_EXECUTABLE!!NC!
        echo !YELLOW!Searching for test executable...!NC!
        set FOUND_EXECUTABLE=
        for /r . %%f in (thread_system_tests.exe thread_system_tests) do (
            if exist "%%f" (
                set TEST_EXECUTABLE=%%f
                set FOUND_EXECUTABLE=true
                echo !GREEN!Found test executable at !TEST_EXECUTABLE!!NC!
                goto :found_executable
            )
        )
        if "!FOUND_EXECUTABLE!"=="" (
            echo !RED!Could not find test executable!!NC!
            exit /b 1
        )
    )
)

:found_executable

:: Run the tests
echo !GREEN!Running tests...!NC!
echo !BLUE!====================================!NC!

:: Create log file
set LOG_FILE=test_output.log
if exist "!LOG_FILE!" del "!LOG_FILE!"

:: Run with appropriate options
if "%VERBOSE%"=="true" (
    "!TEST_EXECUTABLE!" --log_level=all > "!LOG_FILE!" 2>&1
    type "!LOG_FILE!"
) else (
    "!TEST_EXECUTABLE!" > "!LOG_FILE!" 2>&1
)

set TEST_RESULT=!ERRORLEVEL!
echo !BLUE!====================================!NC!

:: Create test_results directory if it doesn't exist
if not exist "..\..\test_results" mkdir "..\..\test_results"

:: Check if there were any failures in the output
set FAILURES=0
for /f %%a in ('findstr /c:" failure" "!LOG_FILE!" 2^>nul ^| find /c /v ""') do set FAILURES=%%a

:: Save test results
if exist "!LOG_FILE!" (
    copy "!LOG_FILE!" "..\..\test_results\thread_system_test_output.txt" > nul
)

:: Extract performance metrics if they exist
echo !YELLOW!Saving test results...!NC!
if exist "!LOG_FILE!" (
    findstr /r /c:"time:" /c:"performance" /c:"tasks:" /c:"queue:" "!LOG_FILE!" > "..\..\test_results\thread_system_performance_metrics.txt" 2>nul
    :: Clean up temporary file
    del "!LOG_FILE!"
)

:: Report test results
if !TEST_RESULT! equ 0 (
    if !FAILURES! equ 0 (
        echo !GREEN!All tests passed!!NC!
    ) else (
        echo !RED!Tests failed! Found !FAILURES! failure^(s^). Please check the output above.!NC!
        set TEST_RESULT=1
    )
) else (
    if !FAILURES! equ 0 (
        echo !RED!Tests failed with exit code !TEST_RESULT!. Please check the output above.!NC!
    ) else (
        echo !RED!Tests failed! Found !FAILURES! failure^(s^). Please check the output above.!NC!
    )
    set TEST_RESULT=1
)

exit /b !TEST_RESULT!