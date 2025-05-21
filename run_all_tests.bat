@echo off
REM run_all_tests.bat - Master script to run all test suites
REM Copyright (c) 2025 Hammer Forged Games

echo Forge Engine Testing Suite
echo =========================
echo.

REM Navigate to project root directory (in case script is run from elsewhere)
cd /d "%~dp0"

REM Create results directory if it doesn't exist
echo Working directory: %CD%
if not exist "test_results" (
    echo Creating results directory: %CD%\test_results
    mkdir test_results
)

REM Create build directory if it doesn't exist
if not exist "build" (
    echo Creating build directory...
    mkdir build
    cmake -B build
)

REM Build all tests
echo Building all tests...
ninja -C build

REM Check if build was successful
if %ERRORLEVEL% neq 0 (
    echo Build failed! Please fix compilation errors.
    exit /b 1
)

echo.
echo Build successful!
echo.

REM Run each test suite and record results

echo ===============================
echo Running AI Optimization Tests...
echo ===============================
bin\debug\ai_optimization_tests.exe > test_results\ai_test_output.txt 2>&1
set AI_RESULT=%ERRORLEVEL%

echo.
echo ===============================
echo Running Save Manager Tests...
echo ===============================
bin\debug\save_manager_tests.exe > test_results\save_test_output.txt 2>&1
set SAVE_RESULT=%ERRORLEVEL%

echo.
echo ===============================
echo Running Thread System Tests...
echo ===============================
bin\debug\thread_system_tests.exe > test_results\thread_test_output.txt 2>&1
set THREAD_RESULT=%ERRORLEVEL%

echo.
echo ===============================
echo TEST RESULTS SUMMARY
echo ===============================

REM Generate timestamp for the report
for /f "tokens=2 delims==" %%a in ('wmic OS Get localdatetime /value') do set "dt=%%a"
set "YYYY=%dt:~0,4%"
set "MM=%dt:~4,2%"
set "DD=%dt:~6,2%"
set "HH=%dt:~8,2%"
set "Min=%dt:~10,2%"
set "Sec=%dt:~12,2%"
set "timestamp=%YYYY%-%MM%-%DD%_%HH%-%Min%-%Sec%"

REM Create a summary report
echo Test Results Summary > test_results\summary_report_%timestamp%.txt
echo =================== >> test_results\summary_report_%timestamp%.txt
echo Generated: %DATE% %TIME% >> test_results\summary_report_%timestamp%.txt
echo. >> test_results\summary_report_%timestamp%.txt

if %AI_RESULT% EQU 0 (
    echo [PASS] AI Optimization Tests
    echo [PASS] AI Optimization Tests >> test_results\summary_report_%timestamp%.txt
) else (
    echo [FAIL] AI Optimization Tests (Error code: %AI_RESULT%)
    echo [FAIL] AI Optimization Tests (Error code: %AI_RESULT%) >> test_results\summary_report_%timestamp%.txt
)

if %SAVE_RESULT% EQU 0 (
    echo [PASS] Save Manager Tests
    echo [PASS] Save Manager Tests >> test_results\summary_report_%timestamp%.txt
) else (
    echo [FAIL] Save Manager Tests (Error code: %SAVE_RESULT%)
    echo [FAIL] Save Manager Tests (Error code: %SAVE_RESULT%) >> test_results\summary_report_%timestamp%.txt
)

if %THREAD_RESULT% EQU 0 (
    echo [PASS] Thread System Tests
    echo [PASS] Thread System Tests >> test_results\summary_report_%timestamp%.txt
) else (
    echo [FAIL] Thread System Tests (Error code: %THREAD_RESULT%)
    echo [FAIL] Thread System Tests (Error code: %THREAD_RESULT%) >> test_results\summary_report_%timestamp%.txt
)

echo. >> test_results\summary_report_%timestamp%.txt
echo Detailed logs available in: >> test_results\summary_report_%timestamp%.txt
echo - test_results\ai_test_output.txt >> test_results\summary_report_%timestamp%.txt
echo - test_results\save_test_output.txt >> test_results\summary_report_%timestamp%.txt
echo - test_results\thread_test_output.txt >> test_results\summary_report_%timestamp%.txt

echo.
echo ===============================
echo All test runs complete!
echo Summary saved to %CD%\test_results\summary_report_%timestamp%.txt
echo ===============================

REM Extract performance metrics (for AI tests)
findstr "processing time:" test_results\ai_test_output.txt > test_results\performance_metrics.txt 2>nul
if %ERRORLEVEL% NEQ 0 (
    REM Try alternative format used by Boost Test
    findstr "processing time" test_results\ai_test_output.txt > test_results\performance_metrics.txt
)
echo Test completed at %DATE% %TIME% >> test_results\performance_metrics.txt

REM Set final exit code based on all test results
if %AI_RESULT% NEQ 0 goto :error
if %SAVE_RESULT% NEQ 0 goto :error
if %THREAD_RESULT% NEQ 0 goto :error
goto :success

:error
echo One or more tests failed. Please check the logs for details.
exit /b 1

:success
echo All tests passed successfully!
exit /b 0