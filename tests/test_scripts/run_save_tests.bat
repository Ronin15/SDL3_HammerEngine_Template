@echo off
:: SaveGameManager Test Runner - Completely Overhauled Version
:: Automatically creates test_data directory and properly detects test failures

setlocal EnableDelayedExpansion

:: Set up colored output
set "RED=[91m"
set "GREEN=[92m"
set "YELLOW=[93m"
set "BLUE=[94m"
set "MAGENTA=[95m"
set "CYAN=[96m"
set "NC=[0m"

:: Navigate to script directory
cd /d "%~dp0"

:: Initialize variables
set CLEAN=false
set CLEAN_ALL=false
set VERBOSE=false
set TEST_FILTER=
set SHOW_HELP=false

:: Parse command line arguments
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
    set SHOW_HELP=true
    shift
    goto :parse_args
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

:: Show help if requested
if "%SHOW_HELP%"=="true" (
    echo !CYAN!SaveGameManager Test Runner!NC!
    echo Usage: %~nx0 [options]
    echo.
    echo !YELLOW!Options:!NC!
    echo   --clean              Clean test artifacts before building
    echo   --clean-all          Remove entire build directory and rebuild
    echo   --verbose            Run tests with verbose output
    echo   --save-test          Run only save/load tests
    echo   --slot-test          Run only slot operations tests
    echo   --error-test         Run only error handling tests
    echo   --serialization-test Run only new serialization system tests
    echo   --performance-test   Run only performance comparison tests
    echo   --integration-test   Run only BinarySerializer integration tests
    echo   --help               Show this help message
    echo.
    echo !BLUE!Example:!NC!
    echo   %~nx0 --verbose --save-test
    exit /b 0
)

echo !CYAN!======================================!NC!
echo !CYAN!    SaveGameManager Test Runner       !NC!
echo !CYAN!======================================!NC!

:: Create test_data directory if it doesn't exist
echo !BLUE!Checking test data directory...!NC!
if not exist "..\test_data" (
    echo !YELLOW!Creating missing test_data directory...!NC!
    mkdir "..\test_data"
    if exist "..\test_data" (
        echo !GREEN!✓ test_data directory created successfully!NC!
    ) else (
        echo !RED!✗ Failed to create test_data directory!NC!
        exit /b 1
    )
) else (
    echo !GREEN!✓ test_data directory already exists!NC!
)

:: Locate test executable
echo !BLUE!Locating test executable...!NC!
set "TEST_EXECUTABLE=..\..\bin\debug\save_manager_tests.exe"
if not exist "!TEST_EXECUTABLE!" (
    set "TEST_EXECUTABLE=..\..\bin\debug\save_manager_tests"
    if not exist "!TEST_EXECUTABLE!" (
        echo !RED!✗ Test executable not found in expected locations!NC!
        echo !YELLOW!Searching project for test executable...!NC!
        set "FOUND_EXECUTABLE="
        for /r ..\.. %%f in (save_manager_tests save_manager_tests.exe) do (
            if exist "%%f" (
                set "TEST_EXECUTABLE=%%f"
                set "FOUND_EXECUTABLE=true"
                echo !GREEN!✓ Found test executable at: %%f!NC!
                goto :found_executable
            )
        )
        if "!FOUND_EXECUTABLE!"=="" (
            echo !RED!✗ Could not find test executable anywhere in project!NC!
            echo !YELLOW!Please build the project first with: ninja save_manager_tests!NC!
            exit /b 1
        )
    )
)

:found_executable
echo !GREEN!✓ Using executable: !TEST_EXECUTABLE!!NC!

:: Prepare test execution
echo !BLUE!Preparing test execution...!NC!
set "TIMESTAMP="
for /f "tokens=2 delims==" %%a in ('wmic OS Get localdatetime /value') do set "dt=%%a"
set "TIMESTAMP=!dt:~0,8!_!dt:~8,6!"

set "LOG_FILE=save_test_output_!TIMESTAMP!.log"
set "RESULTS_DIR=..\..\test_results"

:: Create results directory
if not exist "!RESULTS_DIR!" (
    echo !YELLOW!Creating test results directory...!NC!
    mkdir "!RESULTS_DIR!"
)

:: Clean up any existing log file
if exist "!LOG_FILE!" del "!LOG_FILE!"

:: Execute tests
echo !CYAN!======================================!NC!
echo !CYAN!         EXECUTING TESTS              !NC!
echo !CYAN!======================================!NC!

:: Change to project root directory before running tests (so relative paths work)
pushd ..\..\
echo !YELLOW!Changed to project root directory: %CD%!NC!

:: Update executable path to be relative from project root
set "ROOT_TEST_EXECUTABLE=bin\debug\save_manager_tests.exe"

if "%VERBOSE%"=="true" (
    echo !BLUE!Running tests in verbose mode...!NC!
    "!ROOT_TEST_EXECUTABLE!" !TEST_FILTER! --log_level=all --report_level=detailed --catch_system_errors=no > "!LOG_FILE!" 2>&1
) else (
    echo !BLUE!Running tests...!NC!
    "!ROOT_TEST_EXECUTABLE!" !TEST_FILTER! --report_level=detailed --catch_system_errors=no > "!LOG_FILE!" 2>&1
)

set EXECUTABLE_EXIT_CODE=!ERRORLEVEL!

:: Return to script directory for processing
popd

echo !CYAN!======================================!NC!
echo !CYAN!        ANALYZING RESULTS             !NC!
echo !CYAN!======================================!NC!

:: Display test output if verbose
if "%VERBOSE%"=="true" (
    echo !BLUE!Test Output:!NC!
    type "..\..\!LOG_FILE!"
    echo.
)

:: Analyze test results by examining output content
set TEST_PASSED=false
set ERROR_COUNT=0
set FAILURE_COUNT=0
set PASSED_COUNT=0

:: Count errors and failures
for /f %%i in ('findstr /r /c:"error:" "..\..\!LOG_FILE!" ^| find /c /v ""') do set ERROR_COUNT=%%i
for /f %%i in ('findstr /r /c:"has failed" "..\..\!LOG_FILE!" ^| find /c /v ""') do set FAILURE_COUNT=%%i

:: Check for success indicators
findstr /r /c:"No errors detected" /c:"All.*passed" /c:"has passed with" "..\..\!LOG_FILE!" >nul 2>&1
if !ERRORLEVEL! equ 0 (
    set TEST_PASSED=true
)

:: Additional check - if we have errors but also success messages, prefer the error count
if !ERROR_COUNT! gtr 0 (
    set TEST_PASSED=false
)

:: Save results to permanent location
copy "..\..\!LOG_FILE!" "!RESULTS_DIR!\save_manager_test_output_!TIMESTAMP!.txt" >nul
copy "..\..\!LOG_FILE!" "!RESULTS_DIR!\save_manager_test_output.txt" >nul

:: Extract test case information
echo === Test Cases Executed === > "!RESULTS_DIR!\save_manager_test_cases.txt"
findstr /r /c:"Entering test case" "..\..\!LOG_FILE!" >> "!RESULTS_DIR!\save_manager_test_cases.txt" 2>nul

:: Extract just test case names
findstr /r /c:"Entering test case" "..\..\!LOG_FILE!" > "!RESULTS_DIR!\save_manager_test_cases_run.txt" 2>nul

:: Extract performance metrics
findstr /r /c:"microseconds" /c:"performance" /c:"serialization" "..\..\!LOG_FILE!" > "!RESULTS_DIR!\save_manager_performance_metrics.txt" 2>nul

:: Report results
echo !CYAN!======================================!NC!
echo !CYAN!           TEST RESULTS               !NC!
echo !CYAN!======================================!NC!

if "!TEST_PASSED!"=="true" (
    echo !GREEN!✓ ALL TESTS PASSED SUCCESSFULLY!!NC!
    echo !GREEN!  No test failures detected!NC!
    if !ERROR_COUNT! gtr 0 (
        echo !YELLOW!  Note: !ERROR_COUNT! assertion errors found but tests still passed!NC!
    )
    set FINAL_EXIT_CODE=0
) else (
    echo !RED!✗ TESTS FAILED!!NC!
    if !ERROR_COUNT! gtr 0 (
        echo !RED!  Assertion Errors: !ERROR_COUNT!!NC!
    )
    if !FAILURE_COUNT! gtr 0 (
        echo !RED!  Test Failures: !FAILURE_COUNT!!NC!
    )
    echo !YELLOW!  Check detailed output below for failure information!NC!
    set FINAL_EXIT_CODE=1
)

echo.
echo !BLUE!Test Results Saved To:!NC!
echo   Primary: !RESULTS_DIR!\save_manager_test_output.txt
echo   Timestamped: !RESULTS_DIR!\save_manager_test_output_!TIMESTAMP!.txt

:: Show test cases that were run
echo.
echo !BLUE!Test Cases Executed:!NC!
if exist "!RESULTS_DIR!\save_manager_test_cases_run.txt" (
    for /f "usebackq delims=" %%l in ("!RESULTS_DIR!\save_manager_test_cases_run.txt") do (
        set "line=%%l"
        :: Extract just the test case name from the line
        for /f "tokens=*" %%m in ("!line!") do (
            set "testline=%%m"
            echo   - !testline!
        )
    )
) else (
    echo !YELLOW!  No test case information found!NC!
)

:: Show failure details if tests failed
if "!TEST_PASSED!"=="false" (
    echo.
    echo !RED!FAILURE DETAILS:!NC!
    echo !YELLOW!Recent errors from test output:!NC!
    findstr /r /c:"error:" "..\..\!LOG_FILE!" | findstr /v /c:"No errors detected" >nul 2>&1 && (
        echo !YELLOW!  Error details saved to results file!NC!
    ) || (
        echo !YELLOW!  No specific error details found in output!NC!
    )
    
    echo.
    echo !BLUE!For complete failure analysis, check:!NC!
    echo   !RESULTS_DIR!\save_manager_test_output.txt
)

:: Performance summary
if exist "!RESULTS_DIR!\save_manager_performance_metrics.txt" (
    echo.
    echo !BLUE!Performance Summary:!NC!
    findstr /r /c:"took.*microseconds" "!RESULTS_DIR!\save_manager_performance_metrics.txt" 2>nul || echo !YELLOW!  No performance data available!NC!
)

:: Clean up temporary log file
del "..\..\!LOG_FILE!" 2>nul

echo.
echo !CYAN!======================================!NC!
if "!TEST_PASSED!"=="true" (
    echo !GREEN!         TESTS COMPLETED - SUCCESS   !NC!
) else (
    echo !RED!         TESTS COMPLETED - FAILED     !NC!
)
echo !CYAN!======================================!NC!

exit /b !FINAL_EXIT_CODE!