@echo off
REM GPU Test Runner for SDL3 HammerEngine (Windows)
REM Runs GPU rendering subsystem tests with configurable options

setlocal EnableDelayedExpansion

REM Script directory
set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\.."

REM Default options
set "VERBOSE=false"
set "UNIT_ONLY=false"
set "INTEGRATION_ONLY=false"
set "SYSTEM_ONLY=false"
set "SKIP_GPU=false"
set "BUILD_TYPE=debug"
set "ERRORS_ONLY=false"

REM Parse command line arguments
:parse_args
if "%~1"=="" goto :end_parse
if /i "%~1"=="--verbose" set "VERBOSE=true" & shift & goto :parse_args
if /i "%~1"=="-v" set "VERBOSE=true" & shift & goto :parse_args
if /i "%~1"=="--unit-only" set "UNIT_ONLY=true" & shift & goto :parse_args
if /i "%~1"=="--integration-only" set "INTEGRATION_ONLY=true" & shift & goto :parse_args
if /i "%~1"=="--system-only" set "SYSTEM_ONLY=true" & shift & goto :parse_args
if /i "%~1"=="--skip-gpu" set "SKIP_GPU=true" & shift & goto :parse_args
if /i "%~1"=="--release" set "BUILD_TYPE=release" & shift & goto :parse_args
if /i "%~1"=="--errors-only" set "ERRORS_ONLY=true" & shift & goto :parse_args
if /i "%~1"=="--help" goto :show_help
if /i "%~1"=="-h" goto :show_help
echo Unknown option: %~1
exit /b 1
:end_parse

REM Create results directory
set "RESULTS_DIR=%PROJECT_ROOT%\test_results\gpu"
if not exist "%RESULTS_DIR%" mkdir "%RESULTS_DIR%"

REM Binary directory
set "BIN_DIR=%PROJECT_ROOT%\bin\%BUILD_TYPE%"

echo.
echo ==================================
echo   GPU Test Suite
echo   Build: %BUILD_TYPE%
echo ==================================
echo.

set "TOTAL_FAILED=0"

REM Run unit tests
if not "%INTEGRATION_ONLY%"=="true" if not "%SYSTEM_ONLY%"=="true" (
    echo Running Unit Tests...
    call :run_test gpu_types_tests
    call :run_test gpu_pipeline_config_tests
    echo.
)

REM Run integration tests
if not "%UNIT_ONLY%"=="true" if not "%SYSTEM_ONLY%"=="true" if not "%SKIP_GPU%"=="true" (
    echo Running Integration Tests...
    call :run_test gpu_device_tests
    call :run_test gpu_shader_manager_tests
    call :run_test gpu_resource_tests
    call :run_test gpu_vertex_pool_tests
    call :run_test sprite_batch_tests
    echo.
)

REM Run system tests
if not "%UNIT_ONLY%"=="true" if not "%INTEGRATION_ONLY%"=="true" if not "%SKIP_GPU%"=="true" (
    echo Running System Tests...
    call :run_test gpu_renderer_tests
    echo.
)

REM Summary
echo ==================================
if %TOTAL_FAILED%==0 (
    echo   All tests passed!
) else (
    echo   %TOTAL_FAILED% test suite(s^) failed
)
echo ==================================
echo.
echo Results saved to: %RESULTS_DIR%

exit /b %TOTAL_FAILED%

:run_test
set "TEST_NAME=%~1"
set "TEST_PATH=%BIN_DIR%\%TEST_NAME%.exe"

if not exist "%TEST_PATH%" (
    echo   [SKIP] %TEST_NAME% ^(not built^)
    exit /b 0
)

set "OUTPUT_FILE=%RESULTS_DIR%\%TEST_NAME%_output.txt"
set "TEST_ARGS=--catch_system_errors=no --no_result_code"

if "%VERBOSE%"=="true" set "TEST_ARGS=%TEST_ARGS% --log_level=all"
if "%ERRORS_ONLY%"=="true" set "TEST_ARGS=%TEST_ARGS% --log_level=error"

echo   Running %TEST_NAME%...
"%TEST_PATH%" %TEST_ARGS% > "%OUTPUT_FILE%" 2>&1
if %errorlevel%==0 (
    echo   [PASS] %TEST_NAME%
) else (
    echo   [FAIL] %TEST_NAME%
    set /a "TOTAL_FAILED+=1"
    if "%VERBOSE%"=="true" type "%OUTPUT_FILE%"
)
exit /b 0

:show_help
echo GPU Test Runner for SDL3 HammerEngine
echo.
echo Usage: %~nx0 [options]
echo.
echo Options:
echo   --verbose, -v      Show detailed test output
echo   --unit-only        Run only unit tests (no GPU required)
echo   --integration-only Run only GPU integration tests
echo   --system-only      Run only full frame flow tests
echo   --skip-gpu         Skip tests requiring GPU (for CI)
echo   --release          Run tests in release mode
echo   --errors-only      Show only test failures
echo   --help, -h         Show this help message
echo.
echo Test Categories:
echo   Unit Tests:        GPUTypes, PipelineConfig (no GPU)
echo   Integration Tests: Device, Shaders, Resources, VertexPool, SpriteBatch
echo   System Tests:      GPURenderer full frame flow
exit /b 0
