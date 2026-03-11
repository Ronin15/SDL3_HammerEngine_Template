@echo off
setlocal enabledelayedexpansion

REM GPU Frame Timing Benchmark Runner

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\.."
set "BUILD_TYPE=debug"
set "WARMUP_FRAMES=120"
set "MEASURE_FRAMES=300"
set "QUAD_COUNT=2000"
set "MODE=particle"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="--release" (
    set "BUILD_TYPE=release"
    shift
    goto parse_args
)
if /i "%~1"=="--warmup" (
    set "WARMUP_FRAMES=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="--frames" (
    set "MEASURE_FRAMES=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="--quads" (
    set "QUAD_COUNT=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="--mode" (
    set "MODE=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="--help" goto help
if /i "%~1"=="-h" goto help

echo Unknown option: %~1
exit /b 1

:help
echo GPU Frame Timing Benchmark Runner
echo.
echo Usage: %~nx0 [options]
echo.
echo Options:
echo   --mode NAME      Workload mode: particle, primitive, sprite, ui, mixed
echo   --release        Run the release benchmark binary
echo   --warmup N       Warmup frames before sampling ^(default: 120^)
echo   --frames N       Measured frames ^(default: 300^)
echo   --quads N        Colored quads written per frame ^(default: 2000^)
echo   --help, -h       Show this help message
exit /b 0

:args_done
set "RESULTS_DIR=%PROJECT_ROOT%\test_results\gpu"
if not exist "%RESULTS_DIR%" mkdir "%RESULTS_DIR%"

set "BENCHMARK_EXECUTABLE=%PROJECT_ROOT%\bin\%BUILD_TYPE%\gpu_frame_timing_benchmark.exe"
set "RESULTS_FILE=%RESULTS_DIR%\gpu_frame_timing_benchmark_%BUILD_TYPE%.txt"

echo ==================================
echo   GPU Frame Timing Benchmark
echo   Build: %BUILD_TYPE%
echo ==================================
echo.

if not exist "%BENCHMARK_EXECUTABLE%" (
    echo Benchmark executable not found: %BENCHMARK_EXECUTABLE%
    echo Build it first with:
    echo   ninja -C build gpu_frame_timing_benchmark
    exit /b 1
)

(
    echo GPU Frame Timing Benchmark - %date% %time%
    echo Build: %BUILD_TYPE%
    echo Mode: %MODE%
    echo Warmup frames: %WARMUP_FRAMES%
    echo Measured frames: %MEASURE_FRAMES%
    echo Quads/frame: %QUAD_COUNT%
    echo.
) > "%RESULTS_FILE%"

echo Writing results to: %RESULTS_FILE%
echo.
echo Command: %BENCHMARK_EXECUTABLE% --mode %MODE% --warmup %WARMUP_FRAMES% --frames %MEASURE_FRAMES% --quads %QUAD_COUNT%
echo.

pushd "%PROJECT_ROOT%"
"%BENCHMARK_EXECUTABLE%" --mode %MODE% --warmup %WARMUP_FRAMES% --frames %MEASURE_FRAMES% --quads %QUAD_COUNT% >> "%RESULTS_FILE%" 2>&1
set "RESULT=%ERRORLEVEL%"
popd

type "%RESULTS_FILE%"
echo.

if not "%RESULT%"=="0" (
    echo Benchmark failed with exit code %RESULT%
    exit /b %RESULT%
)

echo Benchmark complete
echo Results saved to: %RESULTS_FILE%
echo Run this from a normal desktop session for meaningful swapchain/VSync timings.
exit /b 0
