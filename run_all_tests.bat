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
set TEST_SCRIPTS[0]=run_thread_tests.bat
set TEST_SCRIPTS[1]=run_thread_safe_ai_tests.bat
set TEST_SCRIPTS[2]=run_thread_safe_ai_integration_tests.bat
set TEST_SCRIPTS[3]=run_ai_benchmark.bat
set TEST_SCRIPTS[4]=run_ai_optimization_tests.bat
set TEST_SCRIPTS[5]=run_save_tests.bat
set SCRIPT_COUNT=6

:: Create a directory for the combined test results
if not exist "test_results\combined" mkdir test_results\combined
set COMBINED_RESULTS=test_results\combined\all_tests_results.txt
echo All Tests Run %date% %time% > "%COMBINED_RESULTS%"

:: Track overall success
set OVERALL_SUCCESS=true
set PASSED_COUNT=0
set FAILED_COUNT=0
set TOTAL_COUNT=%SCRIPT_COUNT%

:: Print header
echo !BLUE!======================================================!NC!
echo !BLUE!              Running All Test Scripts                !NC!
echo !BLUE!======================================================!NC!
echo !YELLOW!Found %SCRIPT_COUNT% test scripts to run!NC!

:: Run each test script
for /L %%i in (0,1,%SCRIPT_COUNT%-1) do (
    call :run_test_script !TEST_SCRIPTS[%%i]!
    
    :: Add a small delay between tests to ensure resources are released
    timeout /t 2 /nobreak >nul
)

:: Print summary
echo.
echo !BLUE!======================================================!NC!
echo !BLUE!                  Test Summary                       !NC!
echo !BLUE!======================================================!NC!
echo Total scripts: %TOTAL_COUNT%
echo !GREEN!Passed: %PASSED_COUNT%!NC!
echo !RED!Failed: %FAILED_COUNT%!NC!

:: Save summary to results file
echo. >> "%COMBINED_RESULTS%"
echo Summary: >> "%COMBINED_RESULTS%"
echo Total: %TOTAL_COUNT% >> "%COMBINED_RESULTS%"
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
    echo !RED!Some test scripts failed. Please check the individual test results.!NC!
    echo !YELLOW!Combined results saved to: %COMBINED_RESULTS%!NC!
    exit /b 1
)

:: Function to run a test script
:run_test_script
setlocal
set script=%~1
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

:: Check if the script exists
if not exist "%script%" (
    echo !RED!Script not found: %script%!NC!
    echo FAILED: Script not found: %script_name% >> "%COMBINED_RESULTS%"
    endlocal
    set OVERALL_SUCCESS=false
    set /a FAILED_COUNT+=1
    exit /b 1
)

:: Run the script with provided arguments
call %script% %args%
set result=%ERRORLEVEL%

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
    endlocal
    set OVERALL_SUCCESS=false
    set /a FAILED_COUNT+=1
    exit /b 1
)