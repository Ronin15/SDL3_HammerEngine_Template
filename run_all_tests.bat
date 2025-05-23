@echo off
setlocal enabledelayedexpansion

:: Script to run all test batch scripts sequentially

:: Set up colored output
set RED=[91m
set GREEN=[92m
set YELLOW=[93m
set BLUE=[94m
set MAGENTA=[95m
set CYAN=[96m
set NC=[0m

:: Directory where all scripts are located
set SCRIPT_DIR=%~dp0

:: Process command line arguments
set CLEAN=false
set CLEAN_ALL=false
set VERBOSE=false

:parse_args
if "%~1"=="" goto :end_parse_args
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
    echo %BLUE%All Tests Runner%NC%
    echo Usage: run_all_tests.bat [options]
    echo.
    echo Options:
    echo   --clean      Clean test artifacts before building
    echo   --clean-all  Remove entire build directory and rebuild
    echo   --verbose    Run tests with verbose output
    echo   --help       Show this help message
    exit /b 0
)
shift
goto :parse_args
:end_parse_args

:: Find all test batch scripts in the directory
set TEST_SCRIPTS=^
  "%SCRIPT_DIR%run_thread_tests.bat" ^
  "%SCRIPT_DIR%run_thread_safe_ai_tests.bat" ^
  "%SCRIPT_DIR%run_thread_safe_ai_integration_tests.bat" ^
  "%SCRIPT_DIR%run_ai_benchmark.bat" ^
  "%SCRIPT_DIR%run_ai_optimization_tests.bat" ^
  "%SCRIPT_DIR%run_save_tests.bat"

:: Create a directory for the combined test results
if not exist "%SCRIPT_DIR%test_results\combined" mkdir "%SCRIPT_DIR%test_results\combined"
set COMBINED_RESULTS=%SCRIPT_DIR%test_results\combined\all_tests_results.txt
echo All Tests Run %date% %time% > "%COMBINED_RESULTS%"

:: Track overall success
set OVERALL_SUCCESS=true
set PASSED_COUNT=0
set FAILED_COUNT=0
set TOTAL_COUNT=6

:: Print header
echo %BLUE%======================================================%NC%
echo %BLUE%              Running All Test Scripts                %NC%
echo %BLUE%======================================================%NC%
echo %YELLOW%Found 6 test scripts to run%NC%

:: Run each test script
for %%s in (%TEST_SCRIPTS%) do (
    call :run_test_script "%%s"
    
    :: Add a small delay between tests to ensure resources are released
    timeout /t 2 /nobreak >nul
)

:: Print summary
echo.
echo %BLUE%======================================================%NC%
echo %BLUE%                  Test Summary                       %NC%
echo %BLUE%======================================================%NC%
echo Total scripts: %TOTAL_COUNT%
echo %GREEN%Passed: %PASSED_COUNT%%NC%
echo %RED%Failed: %FAILED_COUNT%%NC%

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
    echo %GREEN%All test scripts completed successfully!%NC%
    exit /b 0
) else (
    echo.
    echo %RED%Some test scripts failed. Please check the individual test results.%NC%
    echo Combined results saved to: %YELLOW%%COMBINED_RESULTS%%NC%
    exit /b 1
)

:: Function to run a test script
:run_test_script
setlocal
set script=%~1
for %%F in (%script%) do set script_name=%%~nxF
set args=

:: Pass along relevant flags
if "%CLEAN%"=="true" set args=%args% --clean
if "%CLEAN_ALL%"=="true" set args=%args% --clean-all
if "%VERBOSE%"=="true" set args=%args% --verbose

echo.
echo %MAGENTA%=====================================================%NC%
echo %CYAN%Running test script: %YELLOW%%script_name%%NC%
echo %MAGENTA%=====================================================%NC%

:: Check if the script exists
if not exist "%script%" (
    echo %RED%Script not found: %script%%NC%
    echo FAILED: Script not found: %script_name% >> "%COMBINED_RESULTS%"
    endlocal
    set OVERALL_SUCCESS=false
    set /a FAILED_COUNT+=1
    exit /b 1
)

:: Run the script with provided arguments
call %script% %args%
set result=%errorlevel%

if %result% equ 0 (
    echo.
    echo %GREEN%✓ Test script %script_name% completed successfully%NC%
    echo PASSED: %script_name% >> "%COMBINED_RESULTS%"
    endlocal
    set /a PASSED_COUNT+=1
    exit /b 0
) else (
    echo.
    echo %RED%✗ Test script %script_name% failed with exit code %result%%NC%
    echo FAILED: %script_name% (exit code: %result%) >> "%COMBINED_RESULTS%"
    endlocal
    set OVERALL_SUCCESS=false
    set /a FAILED_COUNT+=1
    exit /b 1
)