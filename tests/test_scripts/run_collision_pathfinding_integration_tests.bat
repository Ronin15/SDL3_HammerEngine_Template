@echo off
REM Script to run collision pathfinding integration tests on Windows
REM This script runs comprehensive integration tests for collision and pathfinding systems

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
    echo Collision Pathfinding Integration Tests Runner
    echo Usage: run_collision_pathfinding_integration_tests.bat [options]
    echo.
    echo Options:
    echo   --verbose    Enable verbose output
    echo   --help       Show this help message
    echo.
    echo This script tests the integration between collision detection
    echo and pathfinding systems, including:
    echo   • Obstacle avoidance pathfinding
    echo   • Dynamic obstacle integration
    echo   • Event-driven cache invalidation
    echo   • Concurrent operations stress testing
    echo   • Performance under load
    echo   • Collision layer integration
    goto :eof
)
echo Unknown option: %~1
echo Use --help for usage information
exit /b 1

:done_parsing

REM Create results directory
if not exist "%PROJECT_DIR%\test_results" mkdir "%PROJECT_DIR%\test_results"
set "RESULTS_FILE=%PROJECT_DIR%\test_results\collision_pathfinding_integration_tests_results.txt"

echo ======================================================
echo       Collision Pathfinding Integration Tests
echo ======================================================

REM Check if the test executable exists
set "TEST_EXECUTABLE=%PROJECT_DIR%\bin\debug\collision_pathfinding_integration_tests.exe"

if not exist "%TEST_EXECUTABLE%" (
    echo Test executable not found: %TEST_EXECUTABLE%
    echo Make sure you have built the project with tests enabled.
    echo Run: cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug ^&^& ninja -C build
    exit /b 1
)

REM Run the tests
echo Running collision pathfinding integration tests...
echo Collision Pathfinding Integration Tests - %date% %time% > "%RESULTS_FILE%"

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
    echo ✓ All collision pathfinding integration tests passed successfully!
    echo.
    echo Test Coverage:
    echo   ✓ Obstacle avoidance pathfinding
    echo   ✓ Dynamic obstacle integration
    echo   ✓ Event-driven cache invalidation
    echo   ✓ Concurrent collision and pathfinding operations
    echo   ✓ Performance under load testing
    echo   ✓ Collision layer pathfinding interaction
    echo.
    echo Integration Validation:
    echo   ✓ Real-world collision detection integration
    echo   ✓ Thread safety with concurrent operations
    echo   ✓ Event system integration
    echo   ✓ System performance under stress
) else (
    echo ✗ Some integration tests failed!
    echo.
    echo Possible Issues:
    echo   • System initialization problems
    echo   • Threading or concurrency issues
    echo   • Event system integration problems
    echo   • Performance degradation
)

echo.
echo Results saved to: %RESULTS_FILE%
echo ======================================================

exit /b !RESULT!