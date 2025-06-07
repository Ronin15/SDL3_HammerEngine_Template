@echo off
:: Helper script to build and run SaveGameManager tests

:: Enable color output on Windows 10+ terminals
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
set CLEAN=false
set CLEAN_ALL=false
set VERBOSE=false
set TEST_FILTER=

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--clean" (
    set CLEAN=true
    shift
    goto :parse_args
)
if /i "%~1"=="--clean-all" (
    set CLEAN_ALL=true
    shift
    goto :parse_args
)
if /i "%~1"=="--verbose" (
    set VERBOSE=true
    shift
    goto :parse_args
)
if /i "%~1"=="--help" (
    echo !BLUE!SaveGameManager Test Runner!NC!
    echo Usage: %0 [options]
    echo.
    echo Options:
    echo   --clean      Clean test artifacts before building
    echo   --clean-all  Remove entire build directory and rebuild
    echo   --verbose    Run tests with verbose output
    echo   --save-test  Run only save/load tests
    echo   --slot-test  Run only slot operations tests
    echo   --error-test Run only error handling tests
    echo   --serialization-test Run only new serialization system tests
    echo   --performance-test Run only performance comparison tests
    echo   --integration-test Run only BinarySerializer integration tests
    echo   --help       Show this help message
    exit /b 0
)
if /i "%~1"=="--save-test" (
    set "TEST_FILTER=--run_test=TestSaveAndLoad"
    shift
    goto :parse_args
)
if /i "%~1"=="--slot-test" (
    set "TEST_FILTER=--run_test=TestSlotOperations"
    shift
    goto :parse_args
)
if /i "%~1"=="--error-test" (
    set "TEST_FILTER=--run_test=TestErrorHandling"
    shift
    goto :parse_args
)
if /i "%~1"=="--serialization-test" (
    set "TEST_FILTER=--run_test=TestNewSerializationSystem"
    shift
    goto :parse_args
)
if /i "%~1"=="--performance-test" (
    set "TEST_FILTER=--run_test=TestPerformanceComparison"
    shift
    goto :parse_args
)
if /i "%~1"=="--integration-test" (
    set "TEST_FILTER=--run_test=TestBinarySerializerIntegration"
    shift
    goto :parse_args
)
shift
goto :parse_args

:done_parsing

echo !BLUE!Running SaveGameManager tests...!NC!

:: Check if test executable exists
set "TEST_EXECUTABLE=..\..\bin\debug\save_manager_tests.exe"
if not exist "!TEST_EXECUTABLE!" (
    set "TEST_EXECUTABLE=..\..\bin\debug\save_manager_tests"
    if not exist "!TEST_EXECUTABLE!" (
        echo !RED!Test executable not found at !TEST_EXECUTABLE!!NC!
        echo !YELLOW!Searching for test executable...!NC!
        set "FOUND_EXECUTABLE="
        for /r .. %%f in (save_manager_tests save_manager_tests.exe) do (
            if exist "%%f" (
                set "TEST_EXECUTABLE=%%f"
                set "FOUND_EXECUTABLE=true"
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

:: Run with appropriate options
set "LOG_FILE=test_output.log"
if exist "!LOG_FILE!" del "!LOG_FILE!"

if "%VERBOSE%"=="true" (
    "!TEST_EXECUTABLE!" !TEST_FILTER! --log_level=all --report_level=detailed > "!LOG_FILE!" 2>&1
    type "!LOG_FILE!"
) else (
    :: Use test_log to capture test case entries even in non-verbose mode
    "!TEST_EXECUTABLE!" !TEST_FILTER! --report_level=short --log_level=test_suite > "!LOG_FILE!" 2>&1
)

set TEST_RESULT=!ERRORLEVEL!
echo !BLUE!====================================!NC!

:: Create test_results directory if it doesn't exist
if not exist "..\..\test_results" mkdir "..\..\test_results"

:: Save test results with timestamp
for /f "tokens=2 delims==" %%a in ('wmic OS Get localdatetime /value') do set "dt=%%a"
set "TIMESTAMP=!dt:~0,8!_!dt:~8,6!"
copy test_output.log "..\..\test_results\save_manager_test_output_!TIMESTAMP!.txt" > nul
:: Also save to the standard location for compatibility
copy test_output.log "..\..\test_results\save_manager_test_output.txt" > nul

:: Extract performance metrics, BinarySerializer test info and test cases run
echo !YELLOW!Saving test results...!NC!
findstr /r /c:"time:" /c:"performance" /c:"microseconds" /c:"BinarySerializer" /c:"serialization" /c:"New serialization system" /c:"Binary writer/reader" test_output.log > "..\..\test_results\save_manager_performance_metrics.txt" 2>nul

:: Extract test cases that were run
echo === Test Cases Executed === > "..\..\test_results\save_manager_test_cases.txt"
findstr /r /c:"Entering test case" /c:"Test case.*passed" test_output.log >> "..\..\test_results\save_manager_test_cases.txt" 2>nul

:: Extract just the test case names for easy reporting
findstr /r /c:"Entering test case" test_output.log > "..\..\test_results\save_manager_test_cases_run.txt" 2>nul

:: Clean up temporary file
del "!LOG_FILE!"



:: Report test results
if !TEST_RESULT! equ 0 (
    echo !GREEN!All tests passed!!NC!
    echo !BLUE!Test results saved to:!NC! ..\..\test_results\save_manager_test_output_!TIMESTAMP!.txt
    
    :: Print summary of test cases run
    echo.
    echo !BLUE!Test Cases Run:!NC!
    if exist "..\..\test_results\save_manager_test_cases_run.txt" (
        for /f "usebackq tokens=*" %%l in ("..\..\test_results\save_manager_test_cases_run.txt") do (
            set "line=%%l"
            :: Extract test case name (simplified)
            echo   - !line!
        )
    ) else (
        echo !YELLOW!  No test case details found.!NC!
    )
) else (
    echo !RED!Some tests failed. Please check the output above.!NC!
    echo !YELLOW!Test results saved to:!NC! ..\..\test_results\save_manager_test_output_!TIMESTAMP!.txt
    
    :: Print a summary of failed tests if available
    echo.
    echo !YELLOW!Failed Test Summary:!NC!
    findstr /r /c:"FAILED" /c:"ASSERT" "..\..\test_results\save_manager_test_output.txt" >nul 2>&1 || echo !YELLOW!No specific failure details found.!NC!
    
    :: Print summary of test cases run
    echo.
    echo !BLUE!Test Cases Run:!NC!
    if exist "..\..\test_results\save_manager_test_cases_run.txt" (
        for /f "usebackq tokens=*" %%l in ("..\..\test_results\save_manager_test_cases_run.txt") do (
            set "line=%%l"
            :: Extract test case name (simplified)
            echo   - !line!
        )
    ) else (
        echo !YELLOW!  No test case details found.!NC!
    )
)

exit /b !TEST_RESULT!