@echo off
:: Script to run the Projectile Scaling Benchmark
:: Copyright (c) 2025 Hammer Forged Games, MIT License

setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%..\.."
set "VERBOSE=false"

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--verbose" set "VERBOSE=true"& shift& goto :parse_args
if /i "%~1"=="--help" (
    echo Projectile Scaling Benchmark
    echo Usage: run_projectile_benchmark.bat [--verbose] [--help]
    echo.
    echo Options:
    echo   --verbose    Run benchmarks with verbose output
    echo   --help       Show this help message
    echo.
    echo Benchmark Coverage:
    echo   - ProjectileScaling: Entity counts 100-5000, entities/ms and ns/entity
    echo   - ThreadingModeComparison: Single vs multi-threaded at different counts
    echo   - SIMDThroughput: SIMD 4-wide position integration efficiency
    echo.
    echo Estimated Runtime: ~1 minute
    exit /b 0
)
shift
goto :parse_args

:done_parsing

cd /d "%SCRIPT_DIR%" 2>nul

set "BENCHMARK_EXEC=%PROJECT_DIR%\bin\debug\projectile_scaling_benchmark.exe"

if not exist "!BENCHMARK_EXEC!" (
    echo Error: Benchmark executable not found: !BENCHMARK_EXEC!
    echo Make sure you have built the project with tests enabled.
    echo Run: cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug ^&^& ninja -C build
    exit /b 1
)

if not exist "%PROJECT_DIR%\test_results" mkdir "%PROJECT_DIR%\test_results"

set "TIMESTAMP=%DATE:~10,4%%DATE:~4,2%%DATE:~7,2%_%TIME:~0,2%%TIME:~3,2%%TIME:~6,2%"
set "TIMESTAMP=%TIMESTAMP: =0%"
set "OUTPUT_FILE=%PROJECT_DIR%\test_results\projectile_scaling_benchmark_%TIMESTAMP%.txt"
set "CURRENT_FILE=%PROJECT_DIR%\test_results\projectile_scaling_current.txt"

echo =====================================================
echo          Projectile Scaling Benchmark
echo =====================================================
echo Testing projectile entity throughput and SIMD 4-wide batching

if "!VERBOSE!"=="true" (
    "!BENCHMARK_EXEC!" --log_level=all 2>&1 | tee "!OUTPUT_FILE!"
    set RESULT=!ERRORLEVEL!
) else (
    "!BENCHMARK_EXEC!" --log_level=test_suite > "!OUTPUT_FILE!" 2>&1
    set RESULT=!ERRORLEVEL!
)

:: Copy to current run file for regression detection
copy /y "!OUTPUT_FILE!" "!CURRENT_FILE!" >nul 2>&1

echo.
echo =====================================================
if !RESULT! equ 0 (
    echo Benchmark completed successfully!
    echo.
    echo Benchmark Coverage:
    echo   [OK] Projectile Entity Scaling ^(100-5000 projectiles^)
    echo   [OK] Threading Mode Comparison
    echo   [OK] SIMD 4-Wide Throughput
    echo.
    echo Key Metrics Measured:
    echo   * Entities/ms peak throughput
    echo   * ns/entity at scale
    echo   * SIMD 4-wide batching efficiency curve
) else (
    echo Some benchmarks failed or encountered issues
    echo Check the detailed results in: !OUTPUT_FILE!
)
echo.
echo Results saved to: !OUTPUT_FILE!
echo Current results: !CURRENT_FILE!
echo =====================================================

exit /b !RESULT!
