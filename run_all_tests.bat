@echo off
:: Script to run all test batch scripts sequentially
:: Copyright (c) 2025 Hammer Forged Games, MIT License

:: Enable color output on Windows 10+ terminals
setlocal EnableDelayedExpansion

:: Color codes for Windows
set "GREEN=[92m"
set "YELLOW=[93m"
set "RED=[91m"
set "BLUE=[94m"
set "MAGENTA=[95m"
set "CYAN=[96m"
set "NC=[0m"

:: Navigate to project root directory (in case script is run from elsewhere)
cd /d "%~dp0"

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
    echo !BLUE!All Tests Runner!NC!
    echo Usage: run_all_tests.bat [options]
    echo.
    echo Options:
    echo   --verbose    Run tests with verbose output
    echo   --help       Show this help message
    exit /b 0
)
echo !RED!Unknown option: %1!NC!
echo Usage: %0 [--verbose] [--help]
exit /b 1

:done_parsing

:: Define all test scripts to run
set SCRIPT_COUNT=7
set TEST_SCRIPT_1=run_thread_tests.bat
set TEST_SCRIPT_2=run_thread_safe_ai_tests.bat
set TEST_SCRIPT_3=run_thread_safe_ai_integration_tests.bat
set TEST_SCRIPT_4=run_ai_benchmark.bat
set TEST_SCRIPT_5=run_ai_optimization_tests.bat
set TEST_SCRIPT_6=run_save_tests.bat
set TEST_SCRIPT_7=run_event_tests.bat

:: Create a directory for the combined test results
if not exist "test_results\combined" mkdir test_results\combined
set COMBINED_RESULTS=test_results\combined\all_tests_results.txt
echo All Tests Run %date% %time% > "%COMBINED_RESULTS%"

:: Track overall success
set OVERALL_SUCCESS=true
set PASSED_COUNT=0
set FAILED_COUNT=0

:: Print header
echo !BLUE!======================================================!NC!
echo !BLUE!              Running All Test Scripts                !NC!
echo !BLUE!======================================================!NC!
echo !YELLOW!Found %SCRIPT_COUNT% test scripts to run!NC!

:: Run each test script
for /L %%i in (1,1,%SCRIPT_COUNT%) do (
    call :run_test_script !TEST_SCRIPT_%%i!
    
    :: Check if script execution failed with critical error
    if ERRORLEVEL 2 (
        echo !RED!Critical error detected. Stopping test execution.!NC!
        goto :summary
    )
    
    :: Add a small delay between tests to ensure resources are released
    timeout /t 2 /nobreak >nul
)

:summary

:: Print summary
echo.
echo !BLUE!======================================================!NC!
echo !BLUE!                  Test Summary                       !NC!
echo !BLUE!======================================================!NC!
echo !BLUE!Total scripts: %SCRIPT_COUNT%!NC!
echo !GREEN!Passed: %PASSED_COUNT%!NC!
echo !RED!Failed: %FAILED_COUNT%!NC!

:: Save summary to results file
echo. >> "%COMBINED_RESULTS%"
echo Summary: >> "%COMBINED_RESULTS%"
echo Total scripts: %SCRIPT_COUNT% >> "%COMBINED_RESULTS%"
echo Passed: %PASSED_COUNT% >> "%COMBINED_RESULTS%"
echo Failed: %FAILED_COUNT% >> "%COMBINED_RESULTS%"
echo Completed at: %date% %time% >> "%COMBINED_RESULTS%"

:: Exit with appropriate status code
if "%OVERALL_SUCCESS%"=="true" (
    echo.
    echo !GREEN!All test scripts completed successfully!!NC!
    exit /b 0
) else (
    echo.
    if %FAILED_COUNT% equ %SCRIPT_COUNT% (
        echo !RED!All test scripts failed! This indicates a serious problem.!NC!
    ) else (
        echo !RED!%FAILED_COUNT% out of %SCRIPT_COUNT% test scripts failed.!NC!
    )
    echo !YELLOW!Please check the individual test results.!NC!
    echo !YELLOW!Combined results saved to: %COMBINED_RESULTS%!NC!
    exit /b 1
)

:: Function to run a test script
:run_test_script
setlocal
set script=%~1
if "%script%"=="" (
    echo !RED!Empty script name passed to run_test_script!NC!
    echo FAILED: Empty script name >> "%COMBINED_RESULTS%"
    endlocal
    set OVERALL_SUCCESS=false
    set /a FAILED_COUNT+=1
    exit /b 1
)
if not exist "%script%" (
    echo !RED!Script not found: %script%!NC!
    echo FAILED: Script not found: %script% >> "%COMBINED_RESULTS%"
    endlocal
    set OVERALL_SUCCESS=false
    set /a FAILED_COUNT+=1
    exit /b 1
)
set script_name=%script%
set args=

:: Pass along relevant flags
if "%VERBOSE%"=="true" (
    set args=%args% --verbose
)

echo.
echo !MAGENTA!=====================================================!NC!
echo !CYAN!Running test script: !YELLOW!%script_name%!NC!
echo !MAGENTA!=====================================================!NC!

:: Run the script with provided arguments
echo !YELLOW!Command: %script% %args%!NC!
call .\%script% %args%
set result=%ERRORLEVEL%

:: Handle case where script doesn't exist or isn't executable
if %result% equ 9009 (
    echo !RED!Error: Script execution failed. Command not found or not executable.!NC!
    set result=2
)

if %result% equ 0 (
    echo.
    echo !GREEN!✓ Test script %script_name% completed successfully!NC!
    echo PASSED: %script_name% >> "%COMBINED_RESULTS%"
    endlocal
    set /a PASSED_COUNT+=1
    exit /b 0
) else (
    echo.
    echo !RED!✗ Test script %script_name% failed with exit code %result%!NC!
    echo FAILED: %script_name% (exit code: %result%) >> "%COMBINED_RESULTS%"
    
    :: Return different exit codes based on severity
    if %result% gtr 1 (
        echo !RED!Critical failure detected in %script_name%. This may affect subsequent tests.!NC!
        echo CRITICAL FAILURE: %script_name% requires attention >> "%COMBINED_RESULTS%"
        endlocal
        set OVERALL_SUCCESS=false
        set /a FAILED_COUNT+=1
        exit /b 2
    ) else (
        echo !YELLOW!Test script failed but may not affect other tests. Continuing...!NC!
        endlocal
        set OVERALL_SUCCESS=false
        set /a FAILED_COUNT+=1
        exit /b 1
    )
)