@echo off
:: Script to run collision system scaling benchmarks
:: Tests SAP (Sweep-and-Prune) for MM and Spatial Hash for MS detection

setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%..\.."
set "VERBOSE=false"

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--verbose" set "VERBOSE=true"& shift& goto :parse_args
if /i "%~1"=="--help" (
    echo Collision Scaling Benchmark
    echo Usage: run_collision_scaling_benchmark.bat [--verbose] [--help]
    exit /b 0
)
shift
goto :parse_args

:done_parsing

cd /d "%SCRIPT_DIR%" 2>nul

set "BENCHMARK_EXEC=%PROJECT_DIR%\bin\debug\collision_scaling_benchmark.exe"

if not exist "!BENCHMARK_EXEC!" (
    echo Error: Benchmark executable not found: !BENCHMARK_EXEC!
    exit /b 1
)

if not exist "%PROJECT_DIR%\test_results" mkdir "%PROJECT_DIR%\test_results"
set "OUTPUT_FILE=%PROJECT_DIR%\test_results\collision_scaling_benchmark_output.txt"

echo ======================================================
echo          Collision Scaling Benchmark
echo ======================================================
echo Running collision scaling benchmarks...

if "!VERBOSE!"=="true" (
    "!BENCHMARK_EXEC!" --log_level=all
    set RESULT=!ERRORLEVEL!
) else (
    "!BENCHMARK_EXEC!" --log_level=test_suite > "!OUTPUT_FILE!" 2>&1
    set RESULT=!ERRORLEVEL!
)

echo.
echo ======================================================
if !RESULT! equ 0 (
    echo All collision scaling benchmarks completed successfully!
    echo.
    echo Benchmark Coverage:
    echo   [OK] MM Scaling ^(Sweep-and-Prune^)
    echo   [OK] MS Scaling ^(Spatial Hash^)
    echo   [OK] Combined Scaling ^(Real-world ratios^)
    echo   [OK] Entity Density ^(Distribution effects^)
    echo.
    echo Performance Targets:
    echo   * MM ^(500 movables^): ^< 0.5ms
    echo   * MS ^(1000 statics^): ^< 0.2ms
    echo   * Combined ^(1500 entities^): ^< 0.8ms
    echo   * Sub-quadratic scaling confirmed
) else (
    echo Some benchmarks failed or encountered issues
    echo Check the detailed results in: !OUTPUT_FILE!
)
echo.
echo Results saved to: !OUTPUT_FILE!
echo ======================================================

exit /b !RESULT!
