@echo off
REM Run Resource Manager Tests
REM Copyright (c) 2025 Hammer Forged Games

setlocal EnableDelayedExpansion

REM Set up colored output
set "RED=[91m"
set "GREEN=[92m"
set "YELLOW=[93m"
set "BLUE=[94m"
set "NC=[0m"

echo !BLUE!==========================================!NC!
echo !BLUE!Running Resource Manager System Tests!NC!
echo !BLUE!==========================================!NC!

REM Navigate to script directory
cd /d "%~dp0"

REM Set build type and verbose flag
set "BUILD_TYPE=Debug"
set "VERBOSE=false"

REM Process command line arguments
:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--verbose" (
    set "VERBOSE=true"
    shift
    goto :parse_args
)
if /i "%~1"=="--release" (
    set "BUILD_TYPE=Release"
    shift
    goto :parse_args
)
if /i "%~1"=="--help" (
    echo Resource Manager Test Runner
    echo Usage: run_resource_tests.bat [options]
    echo.
    echo Options:
    echo   --verbose    Run tests with verbose output
    echo   --release    Use release build instead of debug
    echo   --help       Show this help message
    echo.
    echo Test Suites:
    echo   - WorldResourceManager core functionality
    echo   - ResourceTemplateManager tests
    echo   - ResourceTemplateManager JSON tests
    echo   - ResourceFactory tests
    echo   - InventoryComponent tests
    echo   - ResourceChangeEvent tests  
    echo   - Resource Integration tests
    exit /b 0
)
shift
goto :parse_args

:done_parsing

REM Determine executable directory based on build type
if "%BUILD_TYPE%"=="Debug" (
    set "EXEC_DIR=..\..\bin\debug"
) else (
    set "EXEC_DIR=..\..\bin\release"
)

REM Create test_results directory if it doesn't exist
if not exist "..\..\test_results" mkdir "..\..\test_results"

REM Set up test environment
set SDL_VIDEODRIVER=dummy

REM Track overall success
set "OVERALL_SUCCESS=true"
set "PASSED_COUNT=0"
set "FAILED_COUNT=0"

echo !YELLOW!Build type: %BUILD_TYPE%!NC!
echo !YELLOW!Test executable directory: !EXEC_DIR!!NC!
echo.

call :run_test_suite "world_resource_manager_tests" "WorldResourceManager"
call :run_test_suite "resource_template_manager_tests" "ResourceTemplateManager"
call :run_test_suite "resource_template_manager_json_tests" "ResourceTemplateManager JSON"
call :run_test_suite "resource_factory_tests" "ResourceFactory"
call :run_test_suite "inventory_component_tests" "InventoryComponent" 
call :run_test_suite "resource_change_event_tests" "ResourceChangeEvent"
call :run_test_suite "resource_integration_tests" "Resource Integration"

goto :show_summary

:run_test_suite
set "test_name=%~1"
set "display_name=%~2"
set "TEST_EXECUTABLE=!EXEC_DIR!\!test_name!.exe"

echo !GREEN!Running !display_name! Tests...!NC!

REM Check if test executable exists
if not exist "!TEST_EXECUTABLE!" (
    echo !RED!Test executable not found at !TEST_EXECUTABLE!!NC!
    echo !YELLOW!Searching for test executable...!NC!
    set "FOUND_EXECUTABLE="
    for /r ..\.. %%f in (!test_name!.exe) do (
        if exist "%%f" (
            set "TEST_EXECUTABLE=%%f"
            set "FOUND_EXECUTABLE=true"
            echo !GREEN!Found test executable at %%f!NC!
            goto :found_executable
        )
    )
    if "!FOUND_EXECUTABLE!"=="" (
        echo !RED!Could not find test executable!!NC!
        echo !RED!Please build the project first!NC!
        set "OVERALL_SUCCESS=false"
        set /a FAILED_COUNT+=1
        exit /b 1
    ))

:found_executable

REM Set test options based on verbosity
if "!VERBOSE!"=="true" (
    set "TEST_OPTS=--log_level=all --report_level=detailed"
) else (
    set "TEST_OPTS=--log_level=error --report_level=short"
)

REM Run the test and capture output - ensure we run from project root for resource file access
REM Convert relative path to absolute path first
for %%f in ("!TEST_EXECUTABLE!") do set "ABS_TEST_EXECUTABLE=%%~ff"
cd /d "%~dp0..\.."
"!ABS_TEST_EXECUTABLE!" !TEST_OPTS! > "test_results\!test_name!_output.txt" 2>&1
set "EXECUTABLE_EXIT_CODE=!ERRORLEVEL!"
cd /d "%~dp0"

REM Analyze test results by examining output content rather than just exit code
set "TEST_PASSED=false"
set "ERROR_COUNT=0"
set "FAILURE_COUNT=0"

REM Count errors and failures in the output
for /f %%i in ('findstr /r /c:"error:" "..\..\test_results\!test_name!_output.txt" 2^>nul ^| find /c /v ""') do set ERROR_COUNT=%%i
for /f %%i in ('findstr /r /c:"has failed" "..\..\test_results\!test_name!_output.txt" 2^>nul ^| find /c /v ""') do set FAILURE_COUNT=%%i

REM Check for success indicators - look for "has passed" message
findstr /r /c:"has passed with" /c:"All.*passed" /c:"No errors detected" "..\..\test_results\!test_name!_output.txt" >nul 2>&1
if !ERRORLEVEL! equ 0 (
    set "TEST_PASSED=true"
)

REM If we have errors but also success messages, prefer the error count
if !ERROR_COUNT! gtr 0 (
    set "TEST_PASSED=false"
)

REM Display output if verbose or if test failed
if "!VERBOSE!"=="true" (
    type "..\..\test_results\!test_name!_output.txt"
) else (
    if "!TEST_PASSED!"=="false" (
        echo !RED!Test failed, showing output:!NC!
        type "..\..\test_results\!test_name!_output.txt"
    )
)

REM Report result based on content analysis, not just exit code
if "!TEST_PASSED!"=="true" (
    echo !GREEN!+ !display_name! tests passed!!NC!
    set /a PASSED_COUNT+=1
) else (
    echo !RED!- !display_name! tests failed!!NC!
    if !ERROR_COUNT! gtr 0 (
        echo !RED!  Errors found: !ERROR_COUNT!!NC!
    )
    if !FAILURE_COUNT! gtr 0 (
        echo !RED!  Failures found: !FAILURE_COUNT!!NC!
    )
    if !EXECUTABLE_EXIT_CODE! neq 0 (
        echo !YELLOW!  Executable exit code: !EXECUTABLE_EXIT_CODE!!NC!
    )
    set "OVERALL_SUCCESS=false"
    set /a FAILED_COUNT+=1
)
echo.
goto :eof

:show_summary
echo !BLUE!==========================================!NC!
echo !BLUE!         Resource Tests Summary          !NC!
echo !BLUE!==========================================!NC!
echo Total test suites: 7
echo !GREEN!Passed: !PASSED_COUNT!!NC!
echo !RED!Failed: !FAILED_COUNT!!NC!

if "!OVERALL_SUCCESS!"=="true" (
    echo.
    echo !GREEN!All Resource Manager tests completed successfully!!NC!
    echo !BLUE!Test results saved to test_results\ directory!NC!
    exit /b 0
) else (
    echo.
    echo !RED!Some Resource Manager tests failed!!NC!
    echo !YELLOW!Check test results in test_results\ directory for details!NC!
    exit /b 1
)