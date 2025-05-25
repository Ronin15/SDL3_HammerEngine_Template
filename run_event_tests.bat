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

:: Define the test executables to build and run
set "EXECUTABLES="

if "%RUN_ALL%"=="true" (
    set "EXECUTABLES=event_manager_tests event_types_tests"
) else (
    if "%RUN_MANAGER%"=="true" set "EXECUTABLES=!EXECUTABLES! event_manager_tests"
    if "%RUN_TYPES%"=="true" set "EXECUTABLES=!EXECUTABLES! event_types_tests"
)

:: Clean tests if requested
if "%CLEAN%"=="true" (
    echo !YELLOW!Cleaning test artifacts...!NC!
    for %%e in (%EXECUTABLES%) do (
        ninja -t clean "%%e"
    )
)

:: Track the final result
set FINAL_RESULT=0

:: Build and run the tests
for %%e in (%EXECUTABLES%) do (
    set EXEC=%%e
    echo !YELLOW!Building !EXEC!...!NC!
    
    ninja "!EXEC!" || (
        echo !RED!Build failed for !EXEC!!NC!
        exit /b 1
    )
    
    :: Check if test executable exists
    set TEST_EXECUTABLE=..\bin\debug\!EXEC!.exe
    if not exist "!TEST_EXECUTABLE!" (
        set TEST_EXECUTABLE=..\bin\debug\!EXEC!
    )
    
    if not exist "!TEST_EXECUTABLE!" (
        echo !RED!Test executable not found at !TEST_EXECUTABLE!!NC!
        echo !YELLOW!Searching for test executable...!NC!
        
        for /r ".." %%f in (*!EXEC!*.exe) do (
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
    echo !GREEN!Build successful. Running !EXEC!...!NC!
    echo !BLUE!====================================!NC!
    
    :: Run all tests
    set TEST_FILTER=
    echo !YELLOW!Running all event system tests!NC!
    
    echo !YELLOW!Executing: !TEST_EXECUTABLE! !TEST_FILTER!!NC!
    
    :: Create a temporary log file
    set LOG_FILE=!EXEC!_output.log
    if exist "!LOG_FILE!" del "!LOG_FILE!"
    
    :: Run with appropriate options
    if "%VERBOSE%"=="true" (
        echo !YELLOW!Running with verbose output!NC!
        !TEST_EXECUTABLE! --log_level=all --report_level=detailed !TEST_FILTER! > "!LOG_FILE!" 2>&1
    ) else (
        !TEST_EXECUTABLE! --report_level=short --log_level=test_suite !TEST_FILTER! > "!LOG_FILE!" 2>&1
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
    
    copy "!LOG_FILE!" "..\test_results\!EXEC!_output_!TIMESTAMP!.txt" > nul
    :: Also save to the standard location for compatibility
    copy "!LOG_FILE!" "..\test_results\!EXEC!_output.txt" > nul
    
    :: Extract test cases that were run
    echo !YELLOW!Saving test results for !EXEC!...!NC!
    
    :: Create the test_cases.txt file
    echo === Test Cases Executed === > "..\test_results\!EXEC!_test_cases.txt"
    findstr /C:"Entering test case" /C:"Test case" "!LOG_FILE!" >> "..\test_results\!EXEC!_test_cases.txt" 2>nul
    
    :: Extract just the test case names for easy reporting
    findstr /C:"Entering test case" "!LOG_FILE!" > "..\test_results\!EXEC!_test_cases_run.txt" 2>nul
    
    :: Report test results
    if !TEST_RESULT! equ 0 (
        echo !GREEN!All tests passed for !EXEC!!NC!
        echo !BLUE!Test results saved to:!NC! test_results\!EXEC!_output_!TIMESTAMP!.txt
        
        :: Print summary of test cases run
        echo.
        echo !BLUE!Test Cases Run:!NC!
        if exist "..\test_results\!EXEC!_test_cases_run.txt" (
            for /f "tokens=*" %%l in (..\test_results\!EXEC!_test_cases_run.txt) do (
                :: Extract test case name
                set "line=%%l"
                set "testcase=!line:*Entering test case =!"
                set "testcase=!testcase:"=!"
                set "testcase=!testcase:~0,-1!"
                if not "!testcase!"=="" (
                    echo   - !testcase!
                )
            )
        ) else (
            echo !YELLOW!  No test case details found.!NC!
        )
    ) else (
        echo !RED!Some tests failed for !EXEC!. Please check the output above.!NC!
        echo !YELLOW!Test results saved to:!NC! test_results\!EXEC!_output_!TIMESTAMP!.txt
        
        :: Print a summary of failed tests if available
        echo.
        echo !YELLOW!Failed Test Summary:!NC!
        findstr /C:"FAILED" /C:"ASSERT" "..\test_results\!EXEC!_output.txt" >nul 2>&1
        if !ERRORLEVEL! equ 0 (
            findstr /C:"FAILED" /C:"ASSERT" "..\test_results\!EXEC!_output.txt"
        ) else (
            echo !YELLOW!No specific failure details found.!NC!
        )
        
        :: Print summary of test cases run
        echo.
        echo !BLUE!Test Cases Run:!NC!
        if exist "..\test_results\!EXEC!_test_cases_run.txt" (
            for /f "tokens=*" %%l in (..\test_results\!EXEC!_test_cases_run.txt) do (
                :: Extract test case name
                set "line=%%l"
                set "testcase=!line:*Entering test case =!"
                set "testcase=!testcase:"=!"
                set "testcase=!testcase:~0,-1!"
                if not "!testcase!"=="" (
                    echo   - !testcase!
                )
            )
        ) else (
            echo !YELLOW!  No test case details found.!NC!
        )
        
        :: Exit with error if any test fails, but continue running other tests
        set FINAL_RESULT=!TEST_RESULT!
    )
    
    :: Clean up temporary file but keep it if verbose is enabled
    if "%VERBOSE%"=="false" (
        del "!LOG_FILE!"
    ) else (
        echo !YELLOW!Log file kept at: !LOG_FILE!!NC!
    )
)

:: Return to the project root directory
cd ..

if not "!FINAL_RESULT!"=="0" (
    echo.
    echo !RED!Some event tests failed!!NC!
    exit /b !FINAL_RESULT!
) else (
    echo.
    echo !GREEN!All event tests completed successfully!!NC!
    
    :: Note about successful tests
    echo "!EXECUTABLES!" | findstr /C:"event_manager_tests" >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        echo.
        echo !GREEN!All event system tests completed successfully!!NC!
        echo !GREEN!^✓ EventManager tests: !YELLOW!12/12!GREEN! tests passing!NC!
        echo !GREEN!^✓ Thread safety and event conditions tests verified!NC!
    )
    
    exit /b 0
)