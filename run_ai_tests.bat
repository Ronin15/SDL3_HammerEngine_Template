@echo off
REM run_ai_tests.bat - Script to build and run all AI tests
REM Copyright (c) 2025 Hammer Forged Games

echo Building All AI Tests...

REM Navigate to project root directory (in case script is run from elsewhere)
cd /d "%~dp0"

REM Create results directory if it doesn't exist
if not exist "test_results" (
    echo Creating results directory...
    mkdir test_results
)

REM Create build directory if it doesn't exist
if not exist "build" (
    echo Creating build directory...
    mkdir build
    cmake -B build
)

REM Build all AI tests
echo Compiling tests...
ninja -C build ai_optimization_tests ai_scaling_benchmark thread_safe_ai_manager_tests thread_safe_ai_integration_tests

REM Check if build was successful
if %ERRORLEVEL% neq 0 (
    echo Build failed! Please fix compilation errors.
    exit /b 1
)

echo Build successful!

REM Function to run a test and process its output
setlocal EnableDelayedExpansion
goto :RunTests

:RunTest
REM Parameters: TestName ExecutableName
set "test_name=%~1"
set "executable_name=%~2"

echo ===============================
echo Running %test_name%...
echo ===============================

REM Run the test and save output
bin\debug\%executable_name%.exe > test_results\%executable_name%_output.txt 2>&1
set TEST_RESULT=%ERRORLEVEL%

REM Extract performance metrics
findstr "time:" test_results\%executable_name%_output.txt > test_results\%executable_name%_performance_metrics.txt 2>nul
findstr "entities:" test_results\%executable_name%_output.txt >> test_results\%executable_name%_performance_metrics.txt 2>nul
findstr "processing time" test_results\%executable_name%_output.txt >> test_results\%executable_name%_performance_metrics.txt 2>nul
findstr "Concurrent processing time" test_results\%executable_name%_output.txt >> test_results\%executable_name%_performance_metrics.txt 2>nul

REM Check if test was successful
if %TEST_RESULT% neq 0 (
    echo %test_name% failed! See test_results\%executable_name%_output.txt for details.
    echo Test failed with error code %TEST_RESULT% >> test_results\%executable_name%_output.txt
    set "RETURN_CODE=1"
) else (
    echo %test_name% passed successfully!
    echo All tests completed successfully >> test_results\%executable_name%_output.txt
)

goto :eof

:RunTests
set "RETURN_CODE=0"

call :RunTest "AI Optimization Tests" "ai_optimization_tests"
timeout /t 2 /nobreak > nul
call :RunTest "AI Scaling Benchmark" "ai_scaling_benchmark"
timeout /t 2 /nobreak > nul
call :RunTest "Thread-Safe AI Manager Tests" "thread_safe_ai_manager_tests"
timeout /t 2 /nobreak > nul
call :RunTest "Thread-Safe AI Integration Tests" "thread_safe_ai_integration_tests"
timeout /t 2 /nobreak > nul

REM Generate summary report
for /f "tokens=2 delims==" %%a in ('wmic OS Get localdatetime /value') do set "dt=%%a"
set "YYYY=!dt:~0,4!"
set "MM=!dt:~4,2!"
set "DD=!dt:~6,2!"
set "HH=!dt:~8,2!"
set "Min=!dt:~10,2!"
set "Sec=!dt:~12,2!"
set "timestamp=!YYYY!-!MM!-!DD!_!HH!-!Min!-!Sec!"

echo AI Tests Summary > test_results\ai_tests_summary_!timestamp!.txt
echo ============== >> test_results\ai_tests_summary_!timestamp!.txt
echo Generated: %DATE% %TIME% >> test_results\ai_tests_summary_!timestamp!.txt
echo. >> test_results\ai_tests_summary_!timestamp!.txt

REM For simplicity, just list all the test results
type test_results\ai_optimization_tests_output.txt | findstr /C:"test cases" >> test_results\ai_tests_summary_!timestamp!.txt
type test_results\ai_scaling_benchmark_output.txt | findstr /C:"test cases" >> test_results\ai_tests_summary_!timestamp!.txt
type test_results\thread_safe_ai_manager_tests_output.txt | findstr /C:"test cases" >> test_results\ai_tests_summary_!timestamp!.txt
type test_results\thread_safe_ai_integration_tests_output.txt | findstr /C:"test cases" >> test_results\ai_tests_summary_!timestamp!.txt

echo Performance Summary:
echo 1. Entity Component Caching: Reduces map lookups for faster entity-behavior access
echo 2. Batch Processing: Groups similar entities for better cache coherency
echo 3. Early Exit Conditions: Skips unnecessary updates based on custom conditions
echo 4. Message Queue System: Batches messages for efficient delivery
echo 5. Thread-Safe Operations: Concurrent AI behavior processing
echo 6. AI Scaling: Performance with increasing entity counts
echo 7. Thread Integration: Thread system and AI system working together

echo Summary saved to test_results\ai_tests_summary_!timestamp!.txt

if %RETURN_CODE% neq 0 (
    echo Some tests failed! See test results for details.
    exit /b 1
) else (
    echo All tests passed successfully!
    exit /b 0
)