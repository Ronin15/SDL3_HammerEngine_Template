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
ninja -C build ai_optimization_tests save_manager_tests thread_system_tests ai_scaling_benchmark thread_safe_ai_manager_tests thread_safe_ai_integration_tests

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
set AI_RESULT=1
if exist "bin\debug\ai_optimization_tests.exe" (
    bin\debug\ai_optimization_tests.exe > test_results\ai_test_output.txt 2>&1
    set AI_RESULT=%ERRORLEVEL%
) else (
    echo Error: AI test executable not found at bin\debug\ai_optimization_tests.exe
    echo AI test executable not found > test_results\ai_test_output.txt
)
REM Add a slight pause to allow for cleanup
timeout /t 2 /nobreak > nul

echo.
echo ===============================
echo Running Save Manager Tests...
echo ===============================
set SAVE_RESULT=1
if exist "bin\debug\save_manager_tests.exe" (
    bin\debug\save_manager_tests.exe > test_results\save_test_output.txt 2>&1
    set SAVE_RESULT=%ERRORLEVEL%
) else (
    echo Error: Save test executable not found at bin\debug\save_manager_tests.exe
    echo Save test executable not found > test_results\save_test_output.txt
)
REM Add a slight pause to allow for cleanup
timeout /t 2 /nobreak > nul

echo.
echo ===============================
echo Running Thread System Tests...
echo ===============================
set THREAD_RESULT=1
if exist "bin\debug\thread_system_tests.exe" (
    bin\debug\thread_system_tests.exe > test_results\thread_test_output.txt 2>&1
    set THREAD_RESULT=%ERRORLEVEL%
) else (
    echo Error: Thread test executable not found at bin\debug\thread_system_tests.exe
    echo Thread test executable not found > test_results\thread_test_output.txt
)
REM Add a slight pause to allow for cleanup
timeout /t 2 /nobreak > nul

echo.
echo ===============================
echo Running AI Scaling Benchmark...
echo ===============================
set AI_BENCHMARK_RESULT=1
if exist "bin\debug\ai_scaling_benchmark.exe" (
    bin\debug\ai_scaling_benchmark.exe > test_results\ai_benchmark_output.txt 2>&1
    set AI_BENCHMARK_RESULT=%ERRORLEVEL%
) else (
    echo Error: AI benchmark executable not found at bin\debug\ai_scaling_benchmark.exe
    echo AI benchmark executable not found > test_results\ai_benchmark_output.txt
)
REM Add a slight pause to allow for cleanup
timeout /t 2 /nobreak > nul

echo.
echo ===============================
echo Running Thread-Safe AI Manager Tests...
echo ===============================
set THREAD_SAFE_AI_RESULT=1
if exist "bin\debug\thread_safe_ai_manager_tests.exe" (
    bin\debug\thread_safe_ai_manager_tests.exe > test_results\thread_safe_ai_output.txt 2>&1
    set THREAD_SAFE_AI_RESULT=%ERRORLEVEL%
) else (
    echo Error: Thread-Safe AI Manager test executable not found at bin\debug\thread_safe_ai_manager_tests.exe
    echo Thread-Safe AI Manager test executable not found > test_results\thread_safe_ai_output.txt
)
REM Add a slight pause to allow for cleanup
timeout /t 2 /nobreak > nul

echo.
echo ===============================
echo Running Thread-Safe AI Integration Tests...
echo ===============================
set AI_INTEGRATION_RESULT=1
if exist "bin\debug\thread_safe_ai_integration_tests.exe" (
    bin\debug\thread_safe_ai_integration_tests.exe > test_results\ai_integration_output.txt 2>&1
    set AI_INTEGRATION_RESULT=%ERRORLEVEL%
) else (
    echo Error: Thread-Safe AI Integration test executable not found at bin\debug\thread_safe_ai_integration_tests.exe
    echo Thread-Safe AI Integration test executable not found > test_results\ai_integration_output.txt
)
REM Add a slight pause to allow for cleanup
timeout /t 2 /nobreak > nul

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

if %AI_BENCHMARK_RESULT% EQU 0 (
    echo [PASS] AI Scaling Benchmark
    echo [PASS] AI Scaling Benchmark >> test_results\summary_report_%timestamp%.txt
) else (
    echo [FAIL] AI Scaling Benchmark (Error code: %AI_BENCHMARK_RESULT%)
    echo [FAIL] AI Scaling Benchmark (Error code: %AI_BENCHMARK_RESULT%) >> test_results\summary_report_%timestamp%.txt
)

if %THREAD_SAFE_AI_RESULT% EQU 0 (
    echo [PASS] Thread-Safe AI Manager Tests
    echo [PASS] Thread-Safe AI Manager Tests >> test_results\summary_report_%timestamp%.txt
) else (
    echo [FAIL] Thread-Safe AI Manager Tests (Error code: %THREAD_SAFE_AI_RESULT%)
    echo [FAIL] Thread-Safe AI Manager Tests (Error code: %THREAD_SAFE_AI_RESULT%) >> test_results\summary_report_%timestamp%.txt
)

if %AI_INTEGRATION_RESULT% EQU 0 (
    echo [PASS] Thread-Safe AI Integration Tests
    echo [PASS] Thread-Safe AI Integration Tests >> test_results\summary_report_%timestamp%.txt
) else (
    echo [FAIL] Thread-Safe AI Integration Tests (Error code: %AI_INTEGRATION_RESULT%)
    echo [FAIL] Thread-Safe AI Integration Tests (Error code: %AI_INTEGRATION_RESULT%) >> test_results\summary_report_%timestamp%.txt
)

echo. >> test_results\summary_report_%timestamp%.txt
echo Detailed logs available in: >> test_results\summary_report_%timestamp%.txt
echo - test_results\ai_test_output.txt >> test_results\summary_report_%timestamp%.txt
echo - test_results\save_test_output.txt >> test_results\summary_report_%timestamp%.txt
echo - test_results\thread_test_output.txt >> test_results\summary_report_%timestamp%.txt
echo - test_results\ai_benchmark_output.txt >> test_results\summary_report_%timestamp%.txt
echo - test_results\thread_safe_ai_output.txt >> test_results\summary_report_%timestamp%.txt
echo - test_results\ai_integration_output.txt >> test_results\summary_report_%timestamp%.txt

echo.
echo ===============================
echo All test runs complete!
echo Summary saved to %CD%\test_results\summary_report_%timestamp%.txt
echo ===============================

REM Extract performance metrics (for all tests)
echo ## AI Tests Performance > test_results\performance_metrics.txt
findstr "processing time:" test_results\ai_test_output.txt >> test_results\performance_metrics.txt 2>nul
if %ERRORLEVEL% NEQ 0 (
    REM Try alternative format used by Boost Test
    findstr "processing time" test_results\ai_test_output.txt >> test_results\performance_metrics.txt
)

echo. >> test_results\performance_metrics.txt
echo ## Save Manager Tests Performance >> test_results\performance_metrics.txt
findstr "time:" test_results\save_test_output.txt >> test_results\performance_metrics.txt 2>nul

echo. >> test_results\performance_metrics.txt
echo ## Thread System Tests Performance >> test_results\performance_metrics.txt
findstr "time:" test_results\thread_test_output.txt >> test_results\performance_metrics.txt 2>nul

echo. >> test_results\performance_metrics.txt
echo ## AI Scaling Benchmark Performance >> test_results\performance_metrics.txt
findstr "time:" test_results\ai_benchmark_output.txt >> test_results\performance_metrics.txt 2>nul
findstr "entities:" test_results\ai_benchmark_output.txt >> test_results\performance_metrics.txt 2>nul
findstr "Updates per second" test_results\ai_benchmark_output.txt >> test_results\performance_metrics.txt 2>nul

echo. >> test_results\performance_metrics.txt
echo ## Thread-Safe AI Manager Tests Performance >> test_results\performance_metrics.txt
findstr "Concurrent processing time" test_results\thread_safe_ai_output.txt >> test_results\performance_metrics.txt 2>nul

echo. >> test_results\performance_metrics.txt
echo ## Thread-Safe AI Integration Tests Performance >> test_results\performance_metrics.txt
findstr "time:" test_results\ai_integration_output.txt >> test_results\performance_metrics.txt 2>nul

echo Test completed at %DATE% %TIME% >> test_results\performance_metrics.txt

REM Set final exit code based on all test results
if %AI_RESULT% NEQ 0 goto :error
if %SAVE_RESULT% NEQ 0 goto :error
if %THREAD_RESULT% NEQ 0 goto :error
if %AI_BENCHMARK_RESULT% NEQ 0 goto :error
if %THREAD_SAFE_AI_RESULT% NEQ 0 goto :error
if %AI_INTEGRATION_RESULT% NEQ 0 goto :error
goto :success

:error
echo One or more tests failed. Please check the logs for details.
exit /b 1

:success
echo All tests passed successfully!
exit /b 0