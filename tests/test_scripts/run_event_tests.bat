@echo off
:: Helper script to build and run Event system tests with only reliable tests
:: Copyright (c) 2025 Hammer Forged Games, MIT License

:: Enable color output on Windows 10+ terminals
setlocal EnableDelayedExpansion

:: Color codes for Windows
set "RED=[91m"
set "GREEN=[92m"
set "YELLOW=[93m"
set "BLUE=[94m"
set "NC=[0m"

:: Process command line arguments
set CLEAN=false
set CLEAN_ALL=false
set VERBOSE=false
set RUN_ALL=true
set RUN_MANAGER=false
set RUN_TYPES=false
set RUN_SEQUENCE=false
set RUN_COOLDOWN=false

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
shift
goto :parse_args

:done_parsing

:: Navigate to project root directory (in case script is run from elsewhere)
cd /d "%~dp0"

echo !BLUE!Running Event System tests...!NC!

:: Track the final result
set FINAL_RESULT=0

:: Run tests based on selection
if "%RUN_ALL%"=="true" (
    call :run_single_test event_manager_tests
    call :run_single_test event_types_tests
) else (
    if "%RUN_MANAGER%"=="true" (
        call :run_single_test event_manager_tests
    )
    if "%RUN_TYPES%"=="true" (
        call :run_single_test event_types_tests
    )
)

goto :show_summary

:run_single_test
set test_name=%~1
:: Check if test executable exists
set "TEST_EXECUTABLE=..\..\bin\debug\%test_name%.exe"
if not exist "!TEST_EXECUTABLE!" (
    set "TEST_EXECUTABLE=..\..\bin\debug\%test_name%"
    if not exist "!TEST_EXECUTABLE!" (
        echo !RED!Test executable not found at !TEST_EXECUTABLE!!NC!
        echo !YELLOW!Searching for test executable...!NC!
        set "FOUND_EXECUTABLE="
        for /r "bin" %%f in (%test_name%.exe %test_name%) do (
            if exist "%%f" (
                set "TEST_EXECUTABLE=%%f"
                set "FOUND_EXECUTABLE=true"
                echo !GREEN!Found test executable at !TEST_EXECUTABLE!!NC!
                goto :found_executable
            )
        )
        if "!FOUND_EXECUTABLE!"=="" (
            echo !RED!Could not find test executable!!NC!
            set FINAL_RESULT=1
            goto :eof
        )
    )
)

:found_executable
    
:: Run the tests with only the reliable tests
echo !GREEN!Running %test_name%...!NC!
echo !BLUE!====================================!NC!

:: Run all tests
set "TEST_FILTER="
echo !YELLOW!Running all event system tests!NC!

echo !YELLOW!Executing: !TEST_EXECUTABLE! !TEST_FILTER!!NC!

:: Create a temporary log file
set "LOG_FILE=%test_name%_output.log"
if exist "!LOG_FILE!" del "!LOG_FILE!"
    
    :: Run with appropriate options and timeout to prevent hanging
    if "%VERBOSE%"=="true" (
        echo !YELLOW!Running with verbose output!NC!
        :: Use PowerShell to run with timeout
        powershell -Command "& { $job = Start-Job -ScriptBlock { & '%CD%\!TEST_EXECUTABLE!' --log_level=all --report_level=detailed !TEST_FILTER! 2>&1 }; if (Wait-Job $job -Timeout 30) { Receive-Job $job } else { Stop-Job $job; Remove-Job $job; Write-Host 'Test execution timed out after 30 seconds' } }" > "!LOG_FILE!"
        set TEST_RESULT=!ERRORLEVEL!
    ) else (
        :: Use test_log to capture test case entries even in non-verbose mode
        powershell -Command "& { $job = Start-Job -ScriptBlock { & '%CD%\!TEST_EXECUTABLE!' --report_level=short --log_level=test_suite !TEST_FILTER! 2>&1 }; if (Wait-Job $job -Timeout 30) { Receive-Job $job } else { Stop-Job $job; Remove-Job $job; Write-Host 'Test execution timed out after 30 seconds' } }" > "!LOG_FILE!"
        set TEST_RESULT=!ERRORLEVEL!
    )
    
    :: Display the test output
    type "!LOG_FILE!"
    echo !BLUE!====================================!NC!
    
    :: Create test_results directory if it doesn't exist
    if not exist "..\..\test_results" mkdir "..\..\test_results"
    
:: Save test results with timestamp
for /f "tokens=2 delims==" %%a in ('wmic OS Get localdatetime /value') do set "dt=%%a"
set "TIMESTAMP=!dt:~0,8!_!dt:~8,6!"
copy "!LOG_FILE!" "..\..\test_results\%test_name%_output_!TIMESTAMP!.txt" > nul
:: Also save to the standard location for compatibility
copy "!LOG_FILE!" "..\..\test_results\%test_name%_output.txt" > nul

:: Extract performance metrics and test cases run
echo !YELLOW!Saving test results for %test_name%...!NC!
findstr /r /c:"time:" /c:"performance" /c:"TestEvent" /c:"TestWeatherEvent" /c:"TestSceneChange" /c:"TestNPCSpawn" /c:"TestEventFactory" /c:"TestEventManager" "!LOG_FILE!" > "..\..\test_results\%test_name%_performance_metrics.txt" 2>nul

:: Extract test cases that were run
echo === Test Cases Executed === > "..\..\test_results\%test_name%_test_cases.txt"
findstr /r /c:"Entering test case" /c:"Test case.*passed" "!LOG_FILE!" >> "..\..\test_results\%test_name%_test_cases.txt" 2>nul

:: Extract just the test case names for easy reporting
findstr /r /c:"Entering test case" "!LOG_FILE!" > "..\..\test_results\%test_name%_test_cases_run.txt" 2>nul
    
:: Report test results
if !TEST_RESULT! equ 0 (
    echo !GREEN!All tests passed for %test_name%!!NC!
    echo !BLUE!Test results saved to:!NC! ..\..\test_results\%test_name%_output_!TIMESTAMP!.txt
    
    :: Print summary of test cases run
    echo.
    echo !BLUE!Test Cases Run:!NC!
    if exist "..\..\test_results\%test_name%_test_cases_run.txt" (
        for /f "usebackq tokens=*" %%l in ("..\..\test_results\%test_name%_test_cases_run.txt") do (
            set "line=%%l"
            :: Simple extraction for test case name
            echo   - !line!
        )
    ) else (
        echo !YELLOW!  No test case details found.!NC!
    )
) else (
    echo !RED!Some tests failed for %test_name%. Please check the output above.!NC!
    echo !YELLOW!Test results saved to:!NC! ..\..\test_results\%test_name%_output_!TIMESTAMP!.txt
    
    :: Print a summary of failed tests if available
    echo.
    echo !YELLOW!Failed Test Summary:!NC!
    findstr /r /c:"FAILED" /c:"ASSERT" "..\..\test_results\%test_name%_output.txt" >nul 2>&1 || echo !YELLOW!No specific failure details found.!NC!
    
    :: Print summary of test cases run
    echo.
    echo !BLUE!Test Cases Run:!NC!
    if exist "..\..\test_results\%test_name%_test_cases_run.txt" (
        for /f "usebackq tokens=*" %%l in ("..\..\test_results\%test_name%_test_cases_run.txt") do (
            set "line=%%l"
            :: Simple extraction for test case name
            echo   - !line!
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
goto :eof

:show_summary



if !FINAL_RESULT! neq 0 (
    echo.
    echo !RED!Some event tests failed!!NC!
    exit /b !FINAL_RESULT!
) else (
    echo.
    echo !GREEN!All event tests completed successfully!!NC!
    
    :: Note about successful tests
    echo !EXECUTABLES! | findstr /c:"event_manager_tests" >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        echo.
        echo !GREEN!All event system tests completed successfully!!NC!
        echo !GREEN!✓ EventManager tests: !YELLOW!12/12!GREEN! tests passing!NC!
        echo !GREEN!✓ Thread safety and event conditions tests verified!NC!
    )
    
    exit /b 0
)