@echo off
REM Script to run PathfinderManager and AIManager contention tests on Windows
REM This script runs integration tests for WorkerBudget coordination between systems

setlocal enabledelayedexpansion

REM Navigate to script directory
cd /d "%~dp0"

REM Get the directory where this script is located
set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%..\.."

REM Process command line arguments
set "VERBOSE=false"

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--verbose" (
    set "VERBOSE=true"
    shift
    goto :parse_args
)
if /i "%~1"=="--help" (
    echo PathfinderManager ^& AIManager Contention Tests Runner
    echo Usage: run_pathfinder_ai_contention_tests.bat [options]
    echo.
    echo Options:
    echo   --verbose    Enable verbose output
    echo   --help       Show this help message
    echo.
    echo This script tests WorkerBudget coordination between PathfinderManager
    echo and AIManager under heavy load, including:
    echo   • WorkerBudget allocation verification (19%% pathfinding, 44%% AI)
    echo   • Simultaneous AI and pathfinding load handling
    echo   • Worker starvation prevention
    echo   • Queue pressure coordination
    echo   • Rate limiting (50 requests/frame)
    echo   • Graceful degradation under stress
    goto :eof
)
echo Unknown option: %~1
echo Use --help for usage information
exit /b 1

:done_parsing

REM Create results directory
if not exist "%PROJECT_DIR%\test_results" mkdir "%PROJECT_DIR%\test_results"
set "RESULTS_FILE=%PROJECT_DIR%\test_results\pathfinder_ai_contention_tests_results.txt"

echo ======================================================
echo    PathfinderManager ^& AIManager Contention Tests
echo ======================================================

REM Check if the test executable exists
set "TEST_EXECUTABLE=%PROJECT_DIR%\bin\debug\pathfinder_ai_contention_tests.exe"

if not exist "%TEST_EXECUTABLE%" (
    echo Test executable not found: %TEST_EXECUTABLE%
    echo Make sure you have built the project with tests enabled.
    echo Run: cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug ^&^& ninja -C build
    exit /b 1
)

REM Run the tests
echo Running PathfinderManager ^& AIManager contention tests...
echo PathfinderManager ^& AIManager Contention Tests - %date% %time% > "%RESULTS_FILE%"

if "%VERBOSE%"=="true" (
    echo Verbose mode enabled
    "%TEST_EXECUTABLE%" --log_level=all 2>&1 | powershell -Command "$input | Tee-Object -FilePath '%RESULTS_FILE%' -Append"
    set "RESULT=!ERRORLEVEL!"
) else (
    "%TEST_EXECUTABLE%" --log_level=test_suite 2>&1 | powershell -Command "$input | Tee-Object -FilePath '%RESULTS_FILE%' -Append"
    set "RESULT=!ERRORLEVEL!"
)

echo. >> "%RESULTS_FILE%"
echo Test completed at: %date% %time% >> "%RESULTS_FILE%"
echo Exit code: !RESULT! >> "%RESULTS_FILE%"

REM Report results
echo.
echo ======================================================
if !RESULT! equ 0 (
    echo ✓ All PathfinderManager ^& AIManager contention tests passed!
    echo.
    echo WorkerBudget Integration Verified:
    echo   ✓ Pathfinding worker allocation (19%%)
    echo   ✓ AI worker allocation (44%%)
    echo   ✓ Simultaneous load handling (100-200 requests)
    echo   ✓ Worker starvation prevention
    echo   ✓ Queue pressure coordination
    echo   ✓ Rate limiting (50 requests/frame)
    echo.
    echo Performance Validation:
    echo   ✓ Request batching with adaptive strategies
    echo   ✓ Graceful degradation under high load
    echo   ✓ Normal priority prevents AIManager contention
    echo   ✓ Queue pressure stays below critical threshold
) else (
    echo ✗ Some contention tests failed!
    echo.
    echo Possible Issues:
    echo   • WorkerBudget coordination problems
    echo   • Worker starvation under heavy load
    echo   • Queue overflow or excessive pressure
    echo   • Rate limiting not working correctly
)

echo.
echo Results saved to: %RESULTS_FILE%
echo ======================================================

exit /b !RESULT!
