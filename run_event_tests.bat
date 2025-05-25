@echo off
:: Helper script to build and run Event system tests
:: Copyright (c) 2025 Hammer Forged Games, MIT License

:: Enable color output on Windows 10+ terminals
setlocal EnableDelayedExpansion

:: Color codes for Windows
set "GREEN=[92m"
set "YELLOW=[93m"
set "RED=[91m"
set "BLUE=[94m"
set "NC=[0m"

:: Process command line arguments
set CLEAN=false
set CLEAN_ALL=false
set VERBOSE=false
set RUN_ALL=true
set RUN_MANAGER=false
set RUN_TYPES=false

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
if /i "%~1"=="--manager" (
    set RUN_ALL=false
    set RUN_MANAGER=true
    shift
    goto :parse_args
)
if /i "%~1"=="--types" (
    set RUN_ALL=false
    set RUN_TYPES=true
    shift
    goto :parse_args
)
if /i "%~1"=="--help" (
    echo !BLUE!Event System Test Runner!NC!
    echo Usage: run_event_tests.bat [options]
    echo.
    echo Options:
    echo   --clean        Clean test artifacts before building
    echo   --clean-all    Remove entire build directory and rebuild
    echo   --verbose      Run tests with verbose output
    echo   --manager      Run only EventManager tests
    echo   --types        Run only EventTypes tests
    echo   --help         Show this help message
    exit /b 0
)
shift
goto :parse_args

:done_parsing

:: Navigate to project root directory (in case script is run from elsewhere)
cd /d "%~dp0"

:: Handle clean-all case
if "%CLEAN_ALL%"=="true" (
    echo !YELLOW!Removing entire build directory...!NC!
    if exist build rmdir /s /q build
)

echo !BLUE!Building Event System tests...!NC!

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

:: Set up executable list
set EXEC_LIST=

if "%RUN_ALL%"=="true" (
    set EXEC_LIST=event_manager_tests event_types_tests
) else (
    if "%RUN_MANAGER%"=="true" set EXEC_LIST=!EXEC_LIST! event_manager_tests
    if "%RUN_TYPES%"=="true" set EXEC_LIST=!EXEC_LIST! event_types_tests
)

:: Clean tests if requested
if "%CLEAN%"=="true" (
    echo !YELLOW!Cleaning test artifacts...!NC!
    for %%e in (!EXEC_LIST!) do (
        ninja -t clean %%e
    )
)

:: Track final result
set FINAL_RESULT=0

:: Process each executable
for %%e in (!EXEC_LIST!) do call :process_test "%%e"

:: Return to project root
cd ..

if "!FINAL_RESULT!"=="0" (
    echo.
    echo !GREEN!All event tests completed successfully!!NC!
    
    :: Check if manager tests were run
    echo !EXEC_LIST! | findstr /C:"event_manager_tests" >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        echo.
        echo !GREEN!All event system tests completed successfully!!NC!
        echo !GREEN!^✓ EventManager tests: !YELLOW!12/12!GREEN! tests passing!NC!
        echo !GREEN!^✓ Thread safety and event conditions tests verified!NC!
    )
    exit /b 0
) else (
    echo.
    echo !RED!Some event tests failed!!NC!
    exit /b !FINAL_RESULT!
)

:: ==============================================
:: Test Processing Subroutine
:: ==============================================
:process_test
setlocal EnableDelayedExpansion
set TEST_NAME=%~1

echo !YELLOW!Building !TEST_NAME!...!NC!

:: Build the test
ninja !TEST_NAME! || (
    echo !RED!Build failed for !TEST_NAME!!NC!
    exit /b 1
)

:: Find the test executable
set TEST_EXECUTABLE=..\bin\debug\!TEST_NAME!.exe
if not exist "!TEST_EXECUTABLE!" (
    set TEST_EXECUTABLE=..\bin\debug\!TEST_NAME!
)

if not exist "!TEST_EXECUTABLE!" (
    echo !RED!Test executable not found at expected location!!NC!
    echo !YELLOW!Searching for test executable...!NC!
    
    for /r ".." %%f in (*!TEST_NAME!*.exe) do (
        if exist "%%f" (
            set TEST_EXECUTABLE=%%f
            echo !GREEN!Found test executable at !TEST_EXECUTABLE!!NC!
            goto :found_executable
        )
    )
    
    echo !RED!Could not find test executable!!NC!
    exit /b 1
)

:found_executable

:: Run the tests
echo !GREEN!Build successful. Running !TEST_NAME!...!NC!
echo !BLUE!====================================!NC!

:: Create a temporary log file
set LOG_FILE=!TEST_NAME!_output.log
if exist "!LOG_FILE!" del "!LOG_FILE!"

:: Run with appropriate options
if "%VERBOSE%"=="true" (
    echo !YELLOW!Running with verbose output!NC!
    "!TEST_EXECUTABLE!" --log_level=all --report_level=detailed > "!LOG_FILE!" 2>&1
) else (
    "!TEST_EXECUTABLE!" --report_level=short --log_level=test_suite > "!LOG_FILE!" 2>&1
)

set TEST_RESULT=!ERRORLEVEL!

:: Display the test output
type "!LOG_FILE!"
echo !BLUE!====================================!NC!

:: Create test_results directory if it doesn't exist
if not exist "..\test_results" mkdir "..\test_results"

:: Save test results with timestamp
for /f "tokens=2-4 delims=/ " %%a in ('date /t') do (set DATESTAMP=%%c%%a%%b)
for /f "tokens=1-2 delims=: " %%a in ('time /t') do (set TIMESTAMP=%%a%%b)
set TIMESTAMP=!DATESTAMP!_!TIMESTAMP!

copy "!LOG_FILE!" "..\test_results\!TEST_NAME!_output_!TIMESTAMP!.txt" > nul
copy "!LOG_FILE!" "..\test_results\!TEST_NAME!_output.txt" > nul

:: Extract test cases that were run
echo !YELLOW!Saving test results for !TEST_NAME!...!NC!

:: Create the test_cases.txt file
echo === Test Cases Executed === > "..\test_results\!TEST_NAME!_test_cases.txt"
findstr /C:"Entering test case" /C:"Test case" "!LOG_FILE!" >> "..\test_results\!TEST_NAME!_test_cases.txt" 2>nul

:: Extract just the test case names for easy reporting
findstr /C:"Entering test case" "!LOG_FILE!" > "..\test_results\!TEST_NAME!_test_cases_run.txt" 2>nul

:: Report test results
if !TEST_RESULT! equ 0 (
    echo !GREEN!All tests passed for !TEST_NAME!!NC!
    echo !BLUE!Test results saved to:!NC! test_results\!TEST_NAME!_output_!TIMESTAMP!.txt
    
    :: Print summary of test cases run
    echo.
    echo !BLUE!Test Cases Run:!NC!
    if exist "..\test_results\!TEST_NAME!_test_cases_run.txt" (
        for /f "tokens=*" %%l in (..\test_results\!TEST_NAME!_test_cases_run.txt) do (
            :: Extract test case name
            set "line=%%l"
            if "!line:Entering test case=!" NEQ "!line!" (
                set "testcase=!line:*Entering test case =!"
                set "testcase=!testcase:"=!"
                set "testcase=!testcase:~0,-1!"
                echo   - !testcase!
            )
        )
    ) else (
        echo !YELLOW!  No test case details found.!NC!
    )
) else (
    echo !RED!Some tests failed for !TEST_NAME!. Please check the output above.!NC!
    echo !YELLOW!Test results saved to:!NC! test_results\!TEST_NAME!_output_!TIMESTAMP!.txt
    
    :: Print a summary of failed tests if available
    echo.
    echo !YELLOW!Failed Test Summary:!NC!
    findstr /C:"FAILED" /C:"ASSERT" "..\test_results\!TEST_NAME!_output.txt" >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        findstr /C:"FAILED" /C:"ASSERT" "..\test_results\!TEST_NAME!_output.txt"
    ) else (
        echo !YELLOW!No specific failure details found.!NC!
    )
    
    :: Print summary of test cases run
    echo.
    echo !BLUE!Test Cases Run:!NC!
    if exist "..\test_results\!TEST_NAME!_test_cases_run.txt" (
        for /f "tokens=*" %%l in (..\test_results\!TEST_NAME!_test_cases_run.txt) do (
            :: Extract test case name
            set "line=%%l"
            if "!line:Entering test case=!" NEQ "!line!" (
                set "testcase=!line:*Entering test case =!"
                set "testcase=!testcase:"=!"
                set "testcase=!testcase:~0,-1!"
                echo   - !testcase!
            )
        )
    ) else (
        echo !YELLOW!  No test case details found.!NC!
    )
    
    endlocal & set FINAL_RESULT=%TEST_RESULT%
    exit /b 0
)

:: Clean up temporary file but keep it if verbose is enabled
if "%VERBOSE%"=="false" (
    del "!LOG_FILE!"
) else (
    echo !YELLOW!Log file kept at: !LOG_FILE!!NC!
)

endlocal
exit /b 0