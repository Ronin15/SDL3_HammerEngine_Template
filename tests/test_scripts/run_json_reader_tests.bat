@echo off
rem Helper script to build and run JsonReader tests

setlocal EnableDelayedExpansion

rem Set up colored output
set "RED=[91m"
set "GREEN=[92m"
set "YELLOW=[93m"
set "BLUE=[94m"
set "MAGENTA=[95m"
set "CYAN=[96m"
set "NC=[0m"
rem Process command line arguments
set CLEAN=false
set CLEAN_ALL=false
set VERBOSE=false
set TEST_FILTER=

:parse_args
if "%~1"=="" goto end_parse
if "%~1"=="--clean" (
    set CLEAN=true
    shift /1
    goto parse_args
)
if "%~1"=="--clean-all" (
    set CLEAN_ALL=true
    shift /1
    goto parse_args
)
if "%~1"=="--verbose" (
    set VERBOSE=true
    shift /1
    goto parse_args
)
if "%~1"=="--help" (
    echo JsonReader Test Runner
    echo Usage: run_json_reader_tests.bat [options]
    echo.
    echo Options:
    echo   --clean      Clean test artifacts before building
    echo   --clean-all  Remove entire build directory and rebuild
    echo   --verbose    Run tests with verbose output
    echo   --parse-test Run only JSON parsing tests
    echo   --error-test Run only error handling tests
    echo   --file-test  Run only file loading tests
    echo   --game-test  Run only game data tests
    echo   --help       Show this help message
    exit /b 0
)
if "%~1"=="--parse-test" (
    set TEST_FILTER=--run_test=JsonReaderParsingTests
    shift /1
    goto parse_args
)
if "%~1"=="--error-test" (
    set TEST_FILTER=--run_test=JsonReaderErrorTests
    shift /1
    goto parse_args
)
if "%~1"=="--file-test" (
    set TEST_FILTER=--run_test=JsonReaderFileTests
    shift /1
    goto parse_args
)
if "%~1"=="--game-test" (
    set TEST_FILTER=--run_test=JsonReaderItemExampleTests
    shift /1
    goto parse_args
)
shift /1
goto parse_args

:end_parse

rem Handle clean-all case
if "%CLEAN_ALL%"=="true" (
    echo Removing entire build directory...
    if exist build (
        rmdir /s /q build
        if exist build (
            echo WARNING: Failed to remove build directory - some files may be in use
        ) else (
            echo Build directory removed successfully
        )
    ) else (
        echo Build directory does not exist - nothing to clean
    )
)
echo !BLUE!Running JsonReader tests...!NC!

rem Navigate to script directory to ensure consistent behavior
cd /d "%~dp0"

rem Get the directory where this script is located and find project root
set SCRIPT_DIR=%~dp0
for %%i in ("%SCRIPT_DIR%..\..") do set PROJECT_ROOT=%%~fi
rem Create test_data directory if it doesn't exist
if not exist "%PROJECT_ROOT%\tests\test_data" (
    echo !YELLOW!Creating missing test_data directory...!NC!
    mkdir "%PROJECT_ROOT%\tests\test_data"
    if exist "%PROJECT_ROOT%\tests\test_data" (
        echo !GREEN!test_data directory created successfully!NC!
    ) else (
        echo !RED!Failed to create test_data directory!NC!
        exit /b 1
    )
) else (
    echo !GREEN!test_data directory already exists!NC!
)
rem Check if test executable exists
set TEST_EXECUTABLE=%PROJECT_ROOT%\bin\debug\json_reader_tests.exe
if not exist "%TEST_EXECUTABLE%" (
    echo !RED!Test executable not found at %TEST_EXECUTABLE%!NC!
    echo !YELLOW!Searching for test executable...!NC!
    for /r "%PROJECT_ROOT%" %%f in (json_reader_tests.exe) do (
        set TEST_EXECUTABLE=%%f
        echo !GREEN!Found test executable at %%f!NC!
        goto found_executable
    )
    echo !RED!Could not find test executable!!NC!
    exit /b 1
)
:found_executable

rem Run the tests
echo Running tests...
echo ====================================

rem Change to project root directory before running tests
cd /d "%PROJECT_ROOT%"

rem Run with appropriate options
if "%VERBOSE%"=="true" (
    "%TEST_EXECUTABLE%" %TEST_FILTER% --log_level=all --report_level=detailed > test_output.log 2>&1
) else (
    "%TEST_EXECUTABLE%" %TEST_FILTER% --report_level=short --log_level=test_suite > test_output.log 2>&1
)
set TEST_RESULT=%ERRORLEVEL%
echo ====================================

rem Create test_results directory if it doesn't exist
if not exist "%PROJECT_ROOT%\test_results" (
    echo Creating test_results directory...
    mkdir "%PROJECT_ROOT%\test_results"
    if not exist "%PROJECT_ROOT%\test_results" (
        echo ERROR: Failed to create test_results directory
        exit /b 1
    )
    echo Test_results directory created successfully
)

rem Save test results with timestamp
for /f "tokens=2-4 delims=/ " %%a in ('date /t') do set DATESTAMP=%%c%%a%%b
for /f "tokens=1-2 delims=: " %%a in ('time /t') do set TIMESTAMP=%%a%%b
set TIMESTAMP=%TIMESTAMP: =0%

echo Saving test results...
copy "test_output.log" "test_results\json_reader_test_output_%DATESTAMP%_%TIMESTAMP%.txt"
if %ERRORLEVEL% neq 0 (
    echo WARNING: Failed to copy test output to timestamped file
)

copy "test_output.log" "test_results\json_reader_test_output.txt"
if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to copy test output to main results file
    echo This may indicate a permissions or disk space issue
)
rem Extract test cases that were run
echo Extracting test case information...
echo. > "test_results\json_reader_test_cases.txt"
echo === Test Cases Executed === >> "test_results\json_reader_test_cases.txt"

rem Look for both detailed format (Entering test case) and summary format (Running X test cases)
findstr /C:"Entering test case" /C:"Test case.*passed" "test_output.log" >> "test_results\json_reader_test_cases.txt"
set DETAILED_FOUND=%ERRORLEVEL%

rem If no detailed test cases found, extract from summary format
if %DETAILED_FOUND% neq 0 (
    echo No detailed test case entries found - extracting from summary format
    findstr /C:"Running.*test cases" /C:"No errors detected" /C:"errors detected" "test_output.log" >> "test_results\json_reader_test_cases.txt"
    if %ERRORLEVEL% neq 0 (
        echo WARNING: No test execution indicators found - test may not have run properly
        echo This could indicate the test executable failed to start or crashed early
    ) else (
        echo Test execution detected from summary format
    )
) else (
    echo Detailed test case information extracted successfully
)

rem Extract just the test case names for easy reporting  
findstr /C:"Entering test case" "test_output.log" | findstr /R /C:".*Entering test case" > "test_results\json_reader_test_cases_run.txt"
if %ERRORLEVEL% neq 0 (
    rem If no detailed entries, create a summary entry
    findstr /C:"Running.*test cases" "test_output.log" > "test_results\json_reader_test_cases_run.txt"
    if %ERRORLEVEL% neq 0 (
        echo WARNING: No test case names found - unable to generate test case summary
    )
)
rem Clean up temporary file
if exist "test_output.log" (
    del "test_output.log"
    if exist "test_output.log" (
        echo WARNING: Could not delete temporary log file - file may be in use
    )
)
rem Report test results
if %TEST_RESULT% equ 0 (
    echo.
    echo !GREEN!All tests passed!!NC!
    echo !BLUE!Test results saved to: test_results\json_reader_test_output_%DATESTAMP%_%TIMESTAMP%.txt!NC!
    
    echo.
    echo !BLUE!Test Cases Run:!NC!
    if exist "test_results\json_reader_test_cases_run.txt" (
        for /f "delims=" %%i in (test_results\json_reader_test_cases_run.txt) do echo !CYAN!  - %%i!NC!
    ) else (
        echo !YELLOW!  No test case details found.!NC!
    )
) else (
    echo.
    echo !RED!Some tests failed. Please check the output above.!NC!
    echo !BLUE!Test results saved to: test_results\json_reader_test_output_%DATESTAMP%_%TIMESTAMP%.txt!NC!
    
    echo.
    echo !RED!Failed Test Summary:!NC!
    rem Look for detailed failure messages first
    findstr /C:"FAILED" /C:"ASSERT" "test_results\json_reader_test_output.txt"
    if %ERRORLEVEL% neq 0 (
        rem If no detailed failures, check for summary indicators
        findstr /C:"errors detected" "test_results\json_reader_test_output.txt" | findstr /v /C:"No errors detected"
        if %ERRORLEVEL% neq 0 (
            echo !YELLOW!No specific failure details found in output!NC!
            echo !YELLOW!This may indicate a runtime crash or early termination!NC!
            echo !YELLOW!Check that the test executable exists and runs properly!NC!
        ) else (
            echo !YELLOW!Error summary information found in test output!NC!
        )
    ) else (
        echo !YELLOW!Detailed failure information found in test output!NC!
    )    
    echo.
    echo !BLUE!Test Cases Run:!NC!
    if exist "test_results\json_reader_test_cases_run.txt" (
        for /f "delims=" %%i in (test_results\json_reader_test_cases_run.txt) do echo !CYAN!  - %%i!NC!
    ) else (
        echo !YELLOW!  No test case details found.!NC!
    )
)
exit /b %TEST_RESULT%