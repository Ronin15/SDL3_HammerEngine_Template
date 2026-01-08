@echo off
REM Script to run collision system scaling benchmarks on Windows
REM Tests SAP (Sweep-and-Prune) for MM and Spatial Hash for MS detection

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
    echo Collision Scaling Benchmark Runner
    echo Usage: run_collision_scaling_benchmark.bat [options]
    echo.
    echo Options:
    echo   --verbose    Run benchmarks with verbose output
    echo   --help       Show this help message
    echo.
    echo Benchmark Coverage:
    echo   - MM Scaling: Sweep-and-Prune effectiveness ^(100-10000 movables^)
    echo   - MS Scaling: Spatial Hash performance ^(100-20000 statics^)
    echo   - Combined Scaling: Real-world entity ratios ^(up to 12K entities^)
    echo   - Entity Density: Clustered vs spread distributions
    echo.
    echo Estimated Runtime: 2-3 minutes
    goto :eof
)
echo Unknown option: %~1
echo Use --help for usage information
exit /b 1

:done_parsing

REM Create results directory
if not exist "%PROJECT_DIR%\test_results" mkdir "%PROJECT_DIR%\test_results"
set "RESULTS_FILE=%PROJECT_DIR%\test_results\collision_scaling_benchmark_%date:~-4%%date:~4,2%%date:~7,2%_%time:~0,2%%time:~3,2%%time:~6,2%.txt"
set "RESULTS_FILE=%RESULTS_FILE: =0%"
set "CURRENT_FILE=%PROJECT_DIR%\test_results\collision_scaling_current.txt"

echo ======================================================
echo          Collision Scaling Benchmark
echo ======================================================
echo Testing SAP for MM and Spatial Hash for MS detection

REM Check if the test executable exists
set "TEST_EXECUTABLE=%PROJECT_DIR%\bin\debug\collision_scaling_benchmark.exe"

if not exist "%TEST_EXECUTABLE%" (
    echo Benchmark executable not found: %TEST_EXECUTABLE%
    echo Make sure you have built the project with tests enabled.
    echo Run: cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug ^&^& ninja -C build
    exit /b 1
)

REM Run the benchmarks
echo Starting collision scaling benchmarks...
echo Collision Scaling Benchmark - %date% %time% > "%RESULTS_FILE%"

if "%VERBOSE%"=="true" (
    echo Verbose mode enabled
    "%TEST_EXECUTABLE%" --log_level=all 2>&1 | powershell -Command "$input | Tee-Object -FilePath '%RESULTS_FILE%' -Append"
    set "RESULT=!ERRORLEVEL!"
) else (
    "%TEST_EXECUTABLE%" --log_level=test_suite 2>&1 | powershell -Command "$input | Tee-Object -FilePath '%RESULTS_FILE%' -Append"
    set "RESULT=!ERRORLEVEL!"
)

echo. >> "%RESULTS_FILE%"
echo Benchmark completed at: %date% %time% >> "%RESULTS_FILE%"
echo Exit code: !RESULT! >> "%RESULTS_FILE%"

REM Copy to current file for regression comparison
copy /Y "%RESULTS_FILE%" "%CURRENT_FILE%" >nul

REM Report results
echo.
echo ======================================================
if !RESULT! equ 0 (
    echo All collision scaling benchmarks completed successfully!
    echo.
    echo Benchmark Coverage:
    echo   MM Scaling ^(Sweep-and-Prune^)
    echo   MS Scaling ^(Spatial Hash^)
    echo   Combined Scaling ^(Real-world ratios^)
    echo   Entity Density ^(Distribution effects^)
) else (
    echo Some benchmarks failed or encountered issues
    echo Check the detailed results in: %RESULTS_FILE%
)

echo.
echo Results saved to: %RESULTS_FILE%
echo Current results: %CURRENT_FILE%
echo ======================================================

REM Performance targets
if !RESULT! equ 0 (
    echo.
    echo Performance Targets:
    echo   * MM ^(5000 movables^): ^< 1.5ms
    echo   * MM ^(10000 movables^): ^< 3.0ms
    echo   * MS ^(20K statics^): ^< 0.2ms ^(FLAT scaling^)
    echo   * Combined ^(12K entities^): ^< 1.5ms
    echo   * Sub-quadratic scaling confirmed
)

exit /b !RESULT!
