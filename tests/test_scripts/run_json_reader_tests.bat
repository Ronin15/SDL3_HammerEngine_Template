@echo off
rem Helper script to build and run JsonReader tests

rem Set up colored output variables (limited support on Windows)
rem Note: Windows batch files have limited color support compared to bash

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
    rmdir /s /q build 2>nul
)

echo Running JsonReader tests...

rem Get the directory where this script is located and find project root
set SCRIPT_DIR=%~dp0
for %%i in ("%SCRIPT_DIR%..\..") do set PROJECT_ROOT=%%~fi

rem Create test_data directory if it doesn't exist
if not exist "%PROJECT_ROOT%\tests\test_data" (
    echo Creating missing test_data directory...
    mkdir "%PROJECT_ROOT%\tests\test_data"
    if exist "%PROJECT_ROOT%\tests\test_data" (
        echo test_data directory created successfully
    ) else (
        echo Failed to create test_data directory
        exit /b 1
    )
) else (
    echo test_data directory already exists
)

rem Check if test executable exists
set TEST_EXECUTABLE=%PROJECT_ROOT%\bin\debug\json_reader_tests.exe
if not exist "%TEST_EXECUTABLE%" (
    echo Test executable not found at %TEST_EXECUTABLE%
    echo Searching for test executable...
    for /r "%PROJECT_ROOT%" %%f in (json_reader_tests.exe) do (
        set TEST_EXECUTABLE=%%f
        echo Found test executable at %%f
        goto found_executable
    )
    echo Could not find test executable!
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
    "%TEST_EXECUTABLE%" %TEST_FILTER% --log_level=all > test_output.log 2>&1
) else (
    "%TEST_EXECUTABLE%" %TEST_FILTER% > test_output.log 2>&1
)

set TEST_RESULT=%ERRORLEVEL%
echo ====================================

rem Create test_results directory if it doesn't exist
if not exist "%PROJECT_ROOT%\test_results" mkdir "%PROJECT_ROOT%\test_results"

rem Save test results with timestamp
for /f "tokens=2-4 delims=/ " %%a in ('date /t') do set DATESTAMP=%%c%%a%%b
for /f "tokens=1-2 delims=: " %%a in ('time /t') do set TIMESTAMP=%%a%%b
set TIMESTAMP=%TIMESTAMP: =0%
copy "test_output.log" "test_results\json_reader_test_output_%DATESTAMP%_%TIMESTAMP%.txt" >nul
copy "test_output.log" "test_results\json_reader_test_output.txt" >nul

rem Extract test cases that were run
echo. > "test_results\json_reader_test_cases.txt"
echo === Test Cases Executed === >> "test_results\json_reader_test_cases.txt"
findstr /C:"Entering test case" /C:"Test case.*passed" "test_output.log" >> "test_results\json_reader_test_cases.txt" 2>nul

rem Extract just the test case names for easy reporting
findstr /C:"Entering test case" "test_output.log" | findstr /R /C:".*Entering test case" > "test_results\json_reader_test_cases_run.txt" 2>nul

rem Clean up temporary file
del "test_output.log"

rem Report test results
if %TEST_RESULT% equ 0 (
    echo All tests passed!
    echo Test results saved to: test_results\json_reader_test_output_%DATESTAMP%_%TIMESTAMP%.txt
    
    echo.
    echo Test Cases Run:
    if exist "test_results\json_reader_test_cases_run.txt" (
        for /f "delims=" %%i in (test_results\json_reader_test_cases_run.txt) do echo   - %%i
    ) else (
        echo   No test case details found.
    )
) else (
    echo Some tests failed. Please check the output above.
    echo Test results saved to: test_results\json_reader_test_output_%DATESTAMP%_%TIMESTAMP%.txt
    
    echo.
    echo Failed Test Summary:
    findstr /C:"FAILED" /C:"ASSERT" "test_results\json_reader_test_output.txt" 2>nul || echo No specific failure details found.
    
    echo.
    echo Test Cases Run:
    if exist "test_results\json_reader_test_cases_run.txt" (
        for /f "delims=" %%i in (test_results\json_reader_test_cases_run.txt) do echo   - %%i
    ) else (
        echo   No test case details found.
    )
)

exit /b %TEST_RESULT%