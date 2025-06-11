@echo off
REM Helper script to run Buffer Utilization tests on Windows

REM Set up colored output (basic Windows support)
set "RED=[91m"
set "GREEN=[92m"
set "YELLOW=[93m"
set "BLUE=[94m"
set "NC=[0m"

REM Process command line arguments
set VERBOSE=false

:parse_args
if "%~1"=="" goto end_parse
if "%~1"=="--verbose" (
    set VERBOSE=true
    shift
    goto parse_args
)
if "%~1"=="--help" (
    echo %BLUE%Buffer Utilization Test Runner%NC%
    echo Usage: run_buffer_utilization_tests.bat [options]
    echo.
    echo Options:
    echo   --verbose    Run tests with verbose output
    echo   --help       Show this help message
    echo.
    echo Description:
    echo   Tests WorkerBudget buffer thread utilization system
    echo   Validates dynamic scaling based on workload thresholds
    echo   Verifies correct allocation on various hardware tiers
    exit /b 0
)
shift
goto parse_args
:end_parse

echo %BLUE%Running Buffer Utilization tests...%NC%

REM Get the directory where this script is located and find project root
set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\..\"

REM Check if test executable exists
set "TEST_EXECUTABLE=%PROJECT_ROOT%bin\debug\buffer_utilization_tests.exe"
if not exist "%TEST_EXECUTABLE%" (
    echo %RED%Test executable not found at %TEST_EXECUTABLE%%NC%
    echo %YELLOW%Searching for test executable...%NC%
    
    REM Search for the executable
    for /r "%PROJECT_ROOT%" %%i in (buffer_utilization_tests.exe) do (
        if exist "%%i" (
            set "TEST_EXECUTABLE=%%i"
            echo %GREEN%Found test executable at %%i%NC%
            goto found_executable
        )
    )
    
    echo %RED%Could not find test executable!%NC%
    echo %YELLOW%Make sure the project is built: ninja -C build%NC%
    exit /b 1
)
:found_executable

REM Run the tests
echo %GREEN%Running WorkerBudget buffer utilization tests...%NC%
echo %BLUE%================================================%NC%

REM Create test_results directory if it doesn't exist
if not exist "%PROJECT_ROOT%test_results" mkdir "%PROJECT_ROOT%test_results"

REM Run with appropriate options
if "%VERBOSE%"=="true" (
    "%TEST_EXECUTABLE%" --log_level=all > test_output.log 2>&1
) else (
    "%TEST_EXECUTABLE%" > test_output.log 2>&1
)

set TEST_RESULT=%ERRORLEVEL%
echo %BLUE%================================================%NC%

REM Save test results
echo %YELLOW%Saving test results and allocation metrics...%NC%
if exist test_output.log (
    copy test_output.log "%PROJECT_ROOT%test_results\buffer_utilization_test_output.txt" >nul
    type test_output.log
    del test_output.log
)

REM Report test results
if %TEST_RESULT% equ 0 (
    echo %GREEN%All buffer utilization tests passed!%NC%
    echo %BLUE%Buffer thread system working correctly across all hardware tiers%NC%
) else (
    echo %RED%Tests failed with exit code %TEST_RESULT%. Please check the output above.%NC%
    echo %YELLOW%Check saved results in test_results\ directory for detailed analysis%NC%
)

exit /b %TEST_RESULT%