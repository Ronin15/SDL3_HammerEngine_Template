@echo off
REM Script to run Particle Manager performance benchmarks on Windows
REM Copyright (c) 2025 Hammer Forged Games, MIT License

setlocal enabledelayedexpansion

REM Navigate to script directory
cd /d "%~dp0"

REM Get the directory where this script is located
set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\.."

REM Process command line arguments
set "VERBOSE=false"
set "BUILD_TYPE=Debug"

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--verbose" (
    set "VERBOSE=true"
    shift
    goto :parse_args
)
if /i "%~1"=="--debug" (
    set "BUILD_TYPE=Debug"
    shift
    goto :parse_args
)
if /i "%~1"=="--release" (
    set "BUILD_TYPE=Release"
    shift
    goto :parse_args
)
if /i "%~1"=="--help" (
    echo Particle Manager Performance Benchmark
    echo Usage: run_particle_manager_benchmark.bat [options]
    echo.
    echo Options:
    echo   --verbose    Run tests with verbose output
    echo   --debug      Use debug build ^(default^)
    echo   --release    Use release build
    echo   --help       Show this help message
    echo.
    echo Benchmark Tests:
    echo   Performance Tests: Performance benchmarks and scaling ^(8 tests^)
    echo.
    echo Execution Time:
    echo   Performance tests: ~2-3 minutes
    echo.
    echo Examples:
    echo   run_particle_manager_benchmark.bat              # Run performance benchmarks
    echo   run_particle_manager_benchmark.bat --verbose    # Detailed benchmark output
    goto :eof
)
echo Unknown option: %~1
echo Use --help for usage information
exit /b 1

:done_parsing

echo ======================================================
echo     Particle Manager Performance Benchmark
echo ======================================================

REM Create timestamp for file naming
for /f "tokens=2-4 delims=/ " %%a in ('date /t') do (set mydate=%%c%%a%%b)
for /f "tokens=1-2 delims=/: " %%a in ("%TIME%") do (set mytime=%%a%%b)
set TIMESTAMP=%mydate%_%mytime%

REM Create results directory
if not exist "%PROJECT_ROOT%\test_results" mkdir "%PROJECT_ROOT%\test_results"
if not exist "%PROJECT_ROOT%\test_results\particle_manager" mkdir "%PROJECT_ROOT%\test_results\particle_manager"

REM Define output files
set "TEST_EXECUTABLE=particle_manager_performance_tests"
set "OUTPUT_FILE=%PROJECT_ROOT%\test_results\particle_manager\%TEST_EXECUTABLE%_output.txt"
set "TIMESTAMPED_FILE=%PROJECT_ROOT%\test_results\particle_benchmark_%TIMESTAMP%.txt"
set "METRICS_FILE=%PROJECT_ROOT%\test_results\particle_benchmark_performance_metrics.txt"
set "SUMMARY_FILE=%PROJECT_ROOT%\test_results\particle_benchmark_summary_%TIMESTAMP%.txt"
set "CSV_FILE=%PROJECT_ROOT%\test_results\particle_benchmark.csv"
set "RESULTS_FILE=%PROJECT_ROOT%\test_results\particle_manager\performance_benchmark_results.txt"

echo Execution Plan: Particle Manager performance benchmarks
echo Note: This will take 2-3 minutes to complete
echo Build type: %BUILD_TYPE%

REM Determine the correct path to the test executable
if "%BUILD_TYPE%"=="Debug" (
    set "TEST_PATH=%PROJECT_ROOT%\bin\debug\%TEST_EXECUTABLE%.exe"
) else (
    set "TEST_PATH=%PROJECT_ROOT%\bin\release\%TEST_EXECUTABLE%.exe"
)

echo.
echo =====================================================
echo Running Particle Manager Performance Benchmarks
echo Test Suite: %TEST_EXECUTABLE%
echo =====================================================

REM Check if test executable exists
if not exist "%TEST_PATH%" (
    echo Test executable not found at %TEST_PATH%
    echo Make sure you have built the project with tests enabled.
    echo Run: cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug ^&^& ninja -C build
    echo FAILED: %TEST_EXECUTABLE% - executable not found >> "%RESULTS_FILE%"
    exit /b 1
)

REM Set test command options
set "TEST_OPTS=--log_level=test_suite --catch_system_errors=no"
if "%VERBOSE%"=="true" (
    set "TEST_OPTS=--log_level=all --report_level=detailed"
)

echo Running with options: %TEST_OPTS%

REM Run the tests and capture output
echo ============ PARTICLE MANAGER BENCHMARK START ============ > "%OUTPUT_FILE%"
echo Date: %date% %time% >> "%OUTPUT_FILE%"
echo Build type: %BUILD_TYPE% >> "%OUTPUT_FILE%"
echo Command: %TEST_PATH% %TEST_OPTS% >> "%OUTPUT_FILE%"
echo ========================================================== >> "%OUTPUT_FILE%"
echo. >> "%OUTPUT_FILE%"

"%TEST_PATH%" %TEST_OPTS% 2>&1 | powershell -Command "$input | Tee-Object -FilePath '%OUTPUT_FILE%' -Append"
set test_result=%ERRORLEVEL%

echo. >> "%OUTPUT_FILE%"
echo ============ PARTICLE MANAGER BENCHMARK END ============ >> "%OUTPUT_FILE%"
echo Date: %date% %time% >> "%OUTPUT_FILE%"
echo Exit code: %test_result% >> "%OUTPUT_FILE%"
echo ======================================================== >> "%OUTPUT_FILE%"

echo ====================================

REM Save timestamped copy to main test_results directory
copy "%OUTPUT_FILE%" "%TIMESTAMPED_FILE%" >nul
copy "%OUTPUT_FILE%" "%PROJECT_ROOT%\test_results\particle_manager\%TEST_EXECUTABLE%_output_%TIMESTAMP%.txt" >nul

REM Extract performance metrics and test summary
findstr /i "time: performance ms TestCase Running.*test.*cases failures.*detected No.*errors.*detected particles=" "%OUTPUT_FILE%" > "%PROJECT_ROOT%\test_results\particle_manager\%TEST_EXECUTABLE%_summary.txt"

REM Handle timeout
if %test_result% equ 124 (
    echo Test execution timed out!
    echo FAILED: %TEST_EXECUTABLE% - timed out >> "%RESULTS_FILE%"
    exit /b 1
)

REM Extract performance metrics
echo Extracting performance metrics...
echo ============ PARTICLE MANAGER PERFORMANCE METRICS ============ > "%METRICS_FILE%"
echo Date: %date% %time% >> "%METRICS_FILE%"
echo Build type: %BUILD_TYPE% >> "%METRICS_FILE%"
echo ============================================================== >> "%METRICS_FILE%"
echo. >> "%METRICS_FILE%"

REM Extract key performance data
findstr /i "Update.*time: particles time: ms Created Initial.*particle.*count Final.*particle.*count Cleanup.*time HighCountBench update_avg_ms" "%OUTPUT_FILE%" >> "%METRICS_FILE%"

echo. >> "%METRICS_FILE%"
echo ============ END OF METRICS ============ >> "%METRICS_FILE%"

REM Create CSV file for trackable benchmark data
echo Generating CSV data for tracking...
echo TestName,ParticleCount,UpdateTimeMs,ThroughputMetric,AdditionalInfo > "%CSV_FILE%"

REM Extract data from benchmark tests and create CSV entries
REM TestUpdatePerformance1000Particles
for /f "tokens=3" %%a in ('findstr /C:"Update time:" "%OUTPUT_FILE%" ^| findstr /B /C:"Update time:" ^| findstr /N "^" ^| findstr "^1:"') do set PERF_1000=%%a
for /f "tokens=5" %%a in ('findstr /C:"Testing update performance with" "%OUTPUT_FILE%" ^| findstr /N "^" ^| findstr "^1:"') do set PARTICLES_1000=%%a
if defined PERF_1000 if defined PARTICLES_1000 (
    echo UpdatePerformance,%PARTICLES_1000%,%PERF_1000%,N/A,1K particle test >> "%CSV_FILE%"
)

REM TestUpdatePerformance5000Particles
for /f "tokens=5" %%a in ('findstr /C:"Testing update performance with" "%OUTPUT_FILE%" ^| findstr /N "^" ^| findstr "^2:"') do set PARTICLES_5000=%%a
for /f "tokens=3" %%a in ('findstr /C:"Update time:" "%OUTPUT_FILE%" ^| findstr /B /C:"Update time:" ^| findstr /N "^" ^| findstr "^2:"') do set PERF_5000=%%a
if defined PERF_5000 if defined PARTICLES_5000 (
    echo UpdatePerformance,%PARTICLES_5000%,%PERF_5000%,N/A,5K particle test >> "%CSV_FILE%"
)

REM TestSustainedPerformance
for /f "tokens=2" %%a in ('findstr /C:"Average:" "%OUTPUT_FILE%"') do set SUSTAINED_AVG=%%a
for /f "tokens=2" %%a in ('findstr /C:"Max:" "%OUTPUT_FILE%" ^| findstr /N "^" ^| findstr "^1:"') do set SUSTAINED_MAX=%%a
if defined SUSTAINED_AVG (
    echo SustainedPerformance,1500,%SUSTAINED_AVG%,%SUSTAINED_MAX%,60 frames average/max >> "%CSV_FILE%"
)

REM HighCountBenchmarks - parse the specific format
for /f "tokens=2 delims==" %%a in ('findstr /C:"HighCountBench: target=10000" "%OUTPUT_FILE%" ^| findstr /C:"update_avg_ms"') do (
    set HIGH_10K=%%a
    set HIGH_10K=!HIGH_10K: =!
)
for /f "tokens=2 delims==" %%a in ('findstr /C:"HighCountBench: target=25000" "%OUTPUT_FILE%" ^| findstr /C:"update_avg_ms"') do (
    set HIGH_25K=%%a
    set HIGH_25K=!HIGH_25K: =!
)
for /f "tokens=2 delims==" %%a in ('findstr /C:"HighCountBench: target=50000" "%OUTPUT_FILE%" ^| findstr /C:"update_avg_ms"') do (
    set HIGH_50K=%%a
    set HIGH_50K=!HIGH_50K: =!
)

if defined HIGH_10K (
    for /f "tokens=2 delims=," %%a in ('findstr /C:"HighCountBench: target=10000" "%OUTPUT_FILE%"') do (
        for /f "tokens=2 delims==" %%b in ("%%a") do (
            set ACTUAL_10K=%%b
            set ACTUAL_10K=!ACTUAL_10K: =!
        )
    )
    if defined ACTUAL_10K echo HighCountBench,!ACTUAL_10K!,%HIGH_10K%,N/A,10K target >> "%CSV_FILE%"
)

if defined HIGH_25K (
    for /f "tokens=2 delims=," %%a in ('findstr /C:"HighCountBench: target=25000" "%OUTPUT_FILE%"') do (
        for /f "tokens=2 delims==" %%b in ("%%a") do (
            set ACTUAL_25K=%%b
            set ACTUAL_25K=!ACTUAL_25K: =!
        )
    )
    if defined ACTUAL_25K echo HighCountBench,!ACTUAL_25K!,%HIGH_25K%,N/A,25K target >> "%CSV_FILE%"
)

if defined HIGH_50K (
    for /f "tokens=2 delims=," %%a in ('findstr /C:"HighCountBench: target=50000" "%OUTPUT_FILE%"') do (
        for /f "tokens=2 delims==" %%b in ("%%a") do (
            set ACTUAL_50K=%%b
            set ACTUAL_50K=!ACTUAL_50K: =!
        )
    )
    if defined ACTUAL_50K echo HighCountBench,!ACTUAL_50K!,%HIGH_50K%,N/A,50K target >> "%CSV_FILE%"
)

REM Create summary file
echo ============ PARTICLE MANAGER BENCHMARK SUMMARY ============ > "%SUMMARY_FILE%"
echo Date: %date% %time% >> "%SUMMARY_FILE%"
echo Build type: %BUILD_TYPE% >> "%SUMMARY_FILE%"
echo Exit code: %test_result% >> "%SUMMARY_FILE%"
echo ============================================================ >> "%SUMMARY_FILE%"
echo. >> "%SUMMARY_FILE%"

echo Key Performance Metrics: >> "%SUMMARY_FILE%"
if defined PERF_1000 if defined PARTICLES_1000 (
    echo   1K particles update: %PERF_1000%ms ^(%PARTICLES_1000% particles^) >> "%SUMMARY_FILE%"
)
if defined PERF_5000 if defined PARTICLES_5000 (
    echo   5K particles update: %PERF_5000%ms ^(%PARTICLES_5000% particles^) >> "%SUMMARY_FILE%"
)
if defined SUSTAINED_AVG (
    echo   Sustained performance avg: %SUSTAINED_AVG%ms >> "%SUMMARY_FILE%"
    echo   Sustained performance max: %SUSTAINED_MAX%ms >> "%SUMMARY_FILE%"
)
if defined HIGH_10K (
    echo   High count ^(10K target^): %HIGH_10K%ms >> "%SUMMARY_FILE%"
)
if defined HIGH_25K (
    echo   High count ^(25K target^): %HIGH_25K%ms >> "%SUMMARY_FILE%"
)
if defined HIGH_50K (
    echo   High count ^(50K target^): %HIGH_50K%ms >> "%SUMMARY_FILE%"
)

echo. >> "%SUMMARY_FILE%"

REM Check test results
findstr /i "failure test.*cases.*failed errors.*detected.*[1-9]" "%OUTPUT_FILE%" >nul
set failure_found=%ERRORLEVEL%

if %test_result%==0 (
    if %failure_found% neq 0 (
        echo.
        echo Performance benchmarks completed successfully
        for /f "tokens=2" %%a in ('findstr /i "Running.*test.*cases" "%OUTPUT_FILE%" 2^>nul') do (
            echo All %%a benchmark tests passed
            goto :test_success
        )
        :test_success
        echo PASSED: %TEST_EXECUTABLE% >> "%RESULTS_FILE%"
        echo Status: Benchmark completed successfully >> "%SUMMARY_FILE%"
        set OVERALL_SUCCESS=true
    ) else (
        goto :test_failed
    )
) else (
    :test_failed
    echo.
    echo Performance benchmarks failed
    echo.
    echo Failure Summary:
    findstr /i "failure FAILED error.*in.*:" "%OUTPUT_FILE%" | findstr /n "^" | findstr "^[1-5]:"
    echo FAILED: %TEST_EXECUTABLE% ^(exit code: %test_result%^) >> "%RESULTS_FILE%"
    echo Status: Benchmark failed >> "%SUMMARY_FILE%"
    set OVERALL_SUCCESS=false
)

echo. >> "%SUMMARY_FILE%"
echo ============ END OF SUMMARY ============ >> "%SUMMARY_FILE%"

REM Print summary
echo.
echo ======================================================
echo          Performance Benchmark Summary
echo ======================================================

REM Display performance summary
if exist "%SUMMARY_FILE%" (
    echo.
    echo Performance Metrics Summary:
    type "%SUMMARY_FILE%"
)

REM Display quick performance summary
echo.
echo Quick Performance Summary:
if defined PERF_1000 (
    echo   1K particles: %PERF_1000%ms
)
if defined PERF_5000 (
    echo   5K particles: %PERF_5000%ms
)
if defined SUSTAINED_AVG (
    echo   Sustained avg: %SUSTAINED_AVG%ms ^(max: %SUSTAINED_MAX%ms^)
)
if defined HIGH_10K (
    echo   High count ^(10K^): %HIGH_10K%ms
)

REM Display CSV info
if exist "%CSV_FILE%" (
    echo.
    echo CSV data generated for tracking performance over time
    echo CSV file: %CSV_FILE%
    for /f %%a in ('find /c /v "" ^< "%CSV_FILE%"') do set CSV_LINES=%%a
    set /a DATA_POINTS=CSV_LINES-1
    echo Data points captured: !DATA_POINTS!
)

REM Exit with appropriate status code
if "%OVERALL_SUCCESS%"=="true" (
    echo.
    echo Particle Manager performance benchmarks completed successfully!
    echo.
    echo Results saved to:
    echo   - Timestamped log: %TIMESTAMPED_FILE%
    echo   - Performance metrics: %METRICS_FILE%
    echo   - Summary: %SUMMARY_FILE%
    echo   - CSV data: %CSV_FILE%
    echo   - Local copy: test_results\particle_manager\
    exit /b 0
) else (
    echo.
    echo Particle Manager performance benchmarks failed!
    echo Please check the benchmark results:
    echo   - Full log: %TIMESTAMPED_FILE%
    echo   - Results: %RESULTS_FILE%
    exit /b 1
)
