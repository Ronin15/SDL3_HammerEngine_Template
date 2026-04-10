@echo off
:: Script to run PathfinderManager tests
:: Copyright (c) 2025 Hammer Forged Games, MIT License

setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%..\.."
set "VERBOSE=false"
set "TEST_FILTER="

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--verbose" set "VERBOSE=true"& shift& goto :parse_args
if /i "%~1:~0,11%"=="--run_test=" set "TEST_FILTER=%~1"& shift& goto :parse_args
if /i "%~1"=="--help" (
    echo PathfinderManager Tests Runner
    echo Usage: run_pathfinder_manager_tests.bat [--verbose] [--run_test=^<name^>] [--help]
    echo.
    echo Options:
    echo   --verbose           Run tests with verbose output
    echo   --run_test=^<name^>   Run a specific test case
    echo   --help              Show this help message
    echo.
    echo Test Coverage:
    echo   PathfinderManagerTestSuite:
    echo     - Singleton and initialization
    echo     - Async pathfinding requests
    echo     - Configuration, weight fields, statistics
    echo     - Shutdown, update cycle, retry logic
    echo     - Cache behaviour and burst handling
    echo   PathfinderEventIntegrationTests:
    echo     - Event subscription and handler lifecycle
    echo     - Cache invalidation on collision change
    exit /b 0
)
shift
goto :parse_args

:done_parsing

set "TEST_EXECUTABLE=%PROJECT_DIR%\bin\debug\pathfinder_manager_tests.exe"
if not exist "!TEST_EXECUTABLE!" (
    echo Error: Test executable not found: !TEST_EXECUTABLE!
    echo Make sure you have built the project with tests enabled.
    echo Run: cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug ^&^& ninja -C build
    exit /b 1
)

if not exist "%PROJECT_DIR%\test_results" mkdir "%PROJECT_DIR%\test_results"
set "RESULTS_FILE=%PROJECT_DIR%\test_results\pathfinder_manager_tests_results.txt"

echo ======================================================
echo          PathfinderManager Tests
echo ======================================================

if "!VERBOSE!"=="true" (
    "!TEST_EXECUTABLE!" --log_level=all !TEST_FILTER! 2>&1 | tee "!RESULTS_FILE!"
    set RESULT=!ERRORLEVEL!
) else (
    "!TEST_EXECUTABLE!" --log_level=test_suite !TEST_FILTER! > "!RESULTS_FILE!" 2>&1
    set RESULT=!ERRORLEVEL!
)

echo.
echo ======================================================
if !RESULT! equ 0 (
    echo All PathfinderManager tests passed!
    echo.
    echo Test Coverage:
    echo   [OK] Singleton and initialization
    echo   [OK] Async pathfinding requests
    echo   [OK] Configuration, weight fields, statistics
    echo   [OK] Shutdown, update cycle, retry logic
    echo   [OK] Cache behaviour and burst handling
    echo   [OK] Event integration and handler lifecycle
) else (
    echo Some PathfinderManager tests failed
    echo Check the detailed results in: !RESULTS_FILE!
)
echo Results saved to: !RESULTS_FILE!
echo ======================================================

exit /b !RESULT!
