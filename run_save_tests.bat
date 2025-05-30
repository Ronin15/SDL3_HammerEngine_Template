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
    echo   --dir-test   Run only directory creation tests
    echo   --save-test  Run only save/load tests
    echo   --slot-test  Run only slot operations tests
    echo   --error-test Run only error handling tests
    echo   --help       Show this help message
    exit /b 0
)
if /i "%~1"=="--dir-test" (
    set "TEST_FILTER=--run_test=TestDirectoryCreation"
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
shift
goto :parse_args

:done_parsing

:: Handle clean-all case
if "%CLEAN_ALL%"=="true" (
    echo !YELLOW!Removing entire build directory...!NC!
    if exist build rmdir /s /q build
)

echo !BLUE!Building SaveGameManager tests...!NC!

:: Ensure build directory exists
if not exist "build" (
    mkdir build
    echo !YELLOW!Created build directory!NC!
)

:: Navigate to build directory
cd build || (
    echo !RED!Failed to enter build directory!!NC!
    exit /b 1
)

:: Configure with CMake if needed
if not exist "build.ninja" (
    echo !YELLOW!Configuring project with CMake and Ninja...!NC!
    cmake -G Ninja .. || (
        echo !RED!CMake configuration failed!!NC!
        exit /b 1
    )
)

:: Clean tests if requested
if "%CLEAN%"=="true" (
    echo !YELLOW!Cleaning test artifacts...!NC!
    ninja -t clean save_manager_tests
)

:: Build the tests
echo !YELLOW!Building tests...!NC!
ninja save_manager_tests || (
    echo !RED!Build failed!!NC!
    exit /b 1
)

:: Check if test executable exists
set "TEST_EXECUTABLE=..\bin\debug\save_manager_tests.exe"
if not exist "!TEST_EXECUTABLE!" (
    set "TEST_EXECUTABLE=..\bin\debug\save_manager_tests"
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
echo !GREEN!Build successful. Running tests...!NC!
echo !BLUE!====================================!NC!

:: Run with appropriate options
set "LOG_FILE=test_output.log"
if exist "!LOG_FILE!" del "!LOG_FILE!"

if "%VERBOSE%"=="true" (
    "!TEST_EXECUTABLE!" --log_level=all --report_level=detailed !TEST_FILTER! > "!LOG_FILE!" 2>&1
    type "!LOG_FILE!"
) else (
    :: Use test_log to capture test case entries even in non-verbose mode
    "!TEST_EXECUTABLE!" --report_level=short --log_level=test_suite !TEST_FILTER! > "!LOG_FILE!" 2>&1
)

set TEST_RESULT=!ERRORLEVEL!
echo !BLUE!====================================!NC!

:: Create test_results directory if it doesn't exist
if not exist "..\test_results" mkdir "..\test_results"

:: Save test results with timestamp
for /f "tokens=2 delims==" %%a in ('wmic OS Get localdatetime /value') do set "dt=%%a"
set "TIMESTAMP=!dt:~0,8!_!dt:~8,6!"
copy "!LOG_FILE!" "..\test_results\save_manager_test_output_!TIMESTAMP!.txt" > nul
:: Also save to the standard location for compatibility
copy "!LOG_FILE!" "..\test_results\save_manager_test_output.txt" > nul

:: Extract performance metrics, directory creation test info and test cases run
echo !YELLOW!Saving test results...!NC!
findstr /r /c:"time:" /c:"performance" /c:"saved:" /c:"loaded:" /c:"TestSaveGameManager:" /c:"Directory creation" /c:"ensureDirectory" "!LOG_FILE!" > "..\test_results\save_manager_performance_metrics.txt" 2>nul

:: Extract test cases that were run
echo === Test Cases Executed === > "..\test_results\save_manager_test_cases.txt"
findstr /r /c:"Entering test case" /c:"Test case.*passed" "!LOG_FILE!" >> "..\test_results\save_manager_test_cases.txt" 2>nul

:: Extract just the test case names for easy reporting
findstr /r /c:"Entering test case" "!LOG_FILE!" > "..\test_results\save_manager_test_cases_run.txt" 2>nul

:: Clean up temporary file
del "!LOG_FILE!"

:: Return to project root
cd ..

:: Report test results
if !TEST_RESULT! equ 0 (
    echo !GREEN!All tests passed!!NC!
    echo !BLUE!Test results saved to:!NC! test_results\save_manager_test_output_!TIMESTAMP!.txt
    
    :: Print summary of test cases run
    echo.
    echo !BLUE!Test Cases Run:!NC!
    if exist "test_results\save_manager_test_cases_run.txt" (
        for /f "usebackq tokens=*" %%l in ("test_results\save_manager_test_cases_run.txt") do (
            set "line=%%l"
            :: Extract test case name (simplified)
            echo   - !line!
        )
    ) else (
        echo !YELLOW!  No test case details found.!NC!
    )
) else (
    echo !RED!Some tests failed. Please check the output above.!NC!
    echo !YELLOW!Test results saved to:!NC! test_results\save_manager_test_output_!TIMESTAMP!.txt
    
    :: Print a summary of failed tests if available
    echo.
    echo !YELLOW!Failed Test Summary:!NC!
    findstr /c:"FAILED" /c:"ASSERT" "test_results\save_manager_test_output.txt" 2>nul || echo !YELLOW!No specific failure details found.!NC!
    
    :: Print summary of test cases run
    echo.
    echo !BLUE!Test Cases Run:!NC!
    if exist "test_results\save_manager_test_cases_run.txt" (
        for /f "usebackq tokens=*" %%l in ("test_results\save_manager_test_cases_run.txt") do (
            set "line=%%l"
            :: Extract test case name (simplified)
            echo   - !line!
        )
    ) else (
        echo !YELLOW!  No test case details found.!NC!
    )
)

exit /b !TEST_RESULT!