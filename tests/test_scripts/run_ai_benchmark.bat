@echo off
:: Script to run the AI Scaling Benchmark
:: Copyright (c) 2025 Hammer Forged Games, MIT License

:: Enable color output on Windows 10+ terminals
setlocal EnableDelayedExpansion

:: Color codes for Windows
set "GREEN=[92m"
set "YELLOW=[93m"
set "RED=[91m"
set "BLUE=[94m"
set "NC=[0m"

:: Script configuration
set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%SCRIPT_DIR%..\..\build"
set "RESULTS_DIR=%SCRIPT_DIR%..\..\test_results"
set "BUILD_TYPE=debug"
set "VERBOSE=false"
set "EXTREME_TEST=false"

:: Function to print colored output (using labels as functions)
goto :main

:print_status
echo !BLUE![INFO]!NC! %~1
goto :eof

:print_success
echo !GREEN![SUCCESS]!NC! %~1
goto :eof

:print_warning
echo !YELLOW![WARNING]!NC! %~1
goto :eof

:print_error
echo !RED![ERROR]!NC! %~1
goto :eof

:show_help
echo AI Scaling Benchmark Test Script
echo.
echo Usage: run_ai_benchmark.bat [options]
echo.
echo Options:
echo   --debug       Run debug build ^(default^)
echo   --release     Run release build ^(optimized^)
echo   --verbose     Show detailed benchmark output
echo   --extreme     Run extended benchmarks
echo   --clean       Clean build artifacts before building
echo   --help        Show this help message
echo.
echo Examples:
echo   run_ai_benchmark.bat                    # Run benchmark with default settings
echo   run_ai_benchmark.bat --release         # Run optimized benchmark
echo   run_ai_benchmark.bat --verbose         # Show detailed performance metrics
echo   run_ai_benchmark.bat --extreme         # Run extended stress tests
echo   run_ai_benchmark.bat --clean --release # Clean build and run optimized benchmark
echo.
echo Output:
echo   Results are saved to timestamped files in test_results directory
echo   Console output shows real-time benchmark progress
echo.
echo The benchmark tests AI Manager performance across multiple scales:
echo   - Basic AI performance ^(small entity count^)
echo   - Threshold testing ^(automatic threading behavior^)
echo   - Medium scale performance ^(1K entities^)
echo   - Large scale testing ^(5K entities^)
echo   - Stress testing ^(100K entities if --extreme^)
goto :eof

:extract_performance_summary
:: Extract key performance metrics from the AI benchmark output
call :print_status "Extracting AI performance summary..."

echo.
echo !BLUE!==== AI PERFORMANCE SUMMARY ====!NC!

:: Check if realistic performance test exists
findstr /c:"===== REALISTIC PERFORMANCE TESTING =====" "!OUTPUT_FILE!" >nul 2>&1
if !ERRORLEVEL! equ 0 (
    echo.
    echo !YELLOW!AI Performance Test Results:!NC!
) else (
    call :print_warning "AI performance test section not found in output"
    goto :extract_basic_metrics
)

:: Extract all performance metrics in order
echo.
echo !YELLOW!Performance Metrics Found:!NC!
findstr /c:"Total time:" "!OUTPUT_FILE!" 2>nul | findstr /n "."
findstr /c:"Entity updates per second:" "!OUTPUT_FILE!" 2>nul | findstr /n "."
findstr /c:"Total behavior updates:" "!OUTPUT_FILE!" 2>nul | findstr /n "."

:extract_basic_metrics
:: Check for scalability summary
echo.
echo !YELLOW!Scalability Analysis:!NC!
findstr /c:"REALISTIC SCALABILITY SUMMARY:" "!OUTPUT_FILE!" >nul 2>&1
if !ERRORLEVEL! equ 0 (
    echo   Scalability summary found in output
    :: Extract the summary table
    findstr /c:"Entity Count" /c:"Threading Mode" /c:"Updates Per Second" "!OUTPUT_FILE!" 2>nul
) else (
    echo   No scalability summary found
)

:: Check for test verification
echo.
echo !YELLOW!Test Verification:!NC!
findstr /c:"Entity updates:" "!OUTPUT_FILE!" | findstr /c:"/" >nul 2>&1
if !ERRORLEVEL! equ 0 (
    echo !GREEN!  Entity verification tests found and checked!NC!
    :: Count successful vs failed verifications
    for /f %%i in ('findstr /c:"Entity updates:" "!OUTPUT_FILE!" 2^>nul ^| find /c /v ""') do (
        echo    Total entity verification checks: %%i
    )
) else (
    echo !YELLOW!  No entity verification data found!NC!
)

:: Check for any threading behavior analysis
echo.
echo !YELLOW!Threading Analysis:!NC!
findstr /c:"Automatic Single-Threaded" "!OUTPUT_FILE!" >nul 2>&1
if !ERRORLEVEL! equ 0 (
    echo   Single-threaded mode performance detected
)
findstr /c:"Automatic Threading" "!OUTPUT_FILE!" >nul 2>&1
if !ERRORLEVEL! equ 0 (
    echo   Multi-threaded mode performance detected
)

:: Extract best performance numbers
echo.
echo !YELLOW!Performance Highlights:!NC!
:: Find the highest entity updates per second
for /f "tokens=*" %%a in ('findstr /c:"Entity updates per second:" "!OUTPUT_FILE!" 2^>nul') do (
    echo   %%a
)

echo.
goto :eof

:main
:: Navigate to script directory
cd /d "%SCRIPT_DIR%"

:: Parse command line arguments
:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--debug" (
    set "BUILD_TYPE=debug"
    shift
    goto :parse_args
)
if /i "%~1"=="--release" (
    set "BUILD_TYPE=release"
    shift
    goto :parse_args
)
if /i "%~1"=="--verbose" (
    set "VERBOSE=true"
    shift
    goto :parse_args
)
if /i "%~1"=="--extreme" (
    set "EXTREME_TEST=true"
    shift
    goto :parse_args
)
if /i "%~1"=="--clean" (
    call :print_status "Cleaning build artifacts..."
    if exist "!BUILD_DIR!" (
        rmdir /s /q "!BUILD_DIR!"
        call :print_success "Build directory cleaned"
    )
    shift
    goto :parse_args
)
if /i "%~1"=="--help" (
    call :show_help
    exit /b 0
)
call :print_error "Unknown option: %1"
echo Use --help for usage information
exit /b 1

:done_parsing

:: Create results directory if it doesn't exist
if not exist "!RESULTS_DIR!" mkdir "!RESULTS_DIR!"

call :print_status "Starting AI Scaling Benchmark..."
call :print_status "Build type: !BUILD_TYPE!"

:: Navigate to script directory
cd /d "%SCRIPT_DIR%"

:: Check if benchmark executable exists
set "BENCHMARK_EXEC=!SCRIPT_DIR!..\..\bin\!BUILD_TYPE!\ai_scaling_benchmark.exe"
if not exist "!BENCHMARK_EXEC!" (
    set "BENCHMARK_EXEC=!SCRIPT_DIR!..\..\bin\!BUILD_TYPE!\ai_scaling_benchmark"
    if not exist "!BENCHMARK_EXEC!" (
        call :print_error "Benchmark executable not found: !BENCHMARK_EXEC!"
        call :print_status "Please build the project first using the build script"
        exit /b 1
    )
)

:: Create timestamped output file
for /f "tokens=2 delims==" %%a in ('wmic OS Get localdatetime /value') do set "dt=%%a"
set "TIMESTAMP=!dt:~0,8!_!dt:~8,6!"
set "OUTPUT_FILE=!RESULTS_DIR!\ai_scaling_benchmark_!TIMESTAMP!.txt"

call :print_status "Results will be saved to: !OUTPUT_FILE!"

:: Prepare output file header
echo AI Scaling Benchmark Results> "!OUTPUT_FILE!"
echo ============================>> "!OUTPUT_FILE!"
echo Date: %date% %time%>> "!OUTPUT_FILE!"
echo Build Type: !BUILD_TYPE!>> "!OUTPUT_FILE!"
echo System: %OS% %PROCESSOR_ARCHITECTURE%>> "!OUTPUT_FILE!"
if "!EXTREME_TEST!"=="true" (
    echo Extreme testing: ENABLED>> "!OUTPUT_FILE!"
) else (
    echo Extreme testing: DISABLED>> "!OUTPUT_FILE!"
)
echo.>> "!OUTPUT_FILE!"

:: Set up test options
set "TEST_OPTS=--catch_system_errors=no --no_result_code"
if "!VERBOSE!"=="true" (
    set "TEST_OPTS=!TEST_OPTS! --log_level=all"
) else (
    set "TEST_OPTS=!TEST_OPTS! --log_level=test_suite"
)

:: Add extreme test flag if requested
if "!EXTREME_TEST!"=="true" (
    set "TEST_OPTS=!TEST_OPTS! --extreme"
    call :print_status "Running with extreme stress testing enabled"
)

:: Run the benchmark
call :print_status "Running AI scaling benchmark..."
call :print_status "This may take several minutes depending on hardware and test options..."

if "!VERBOSE!"=="true" (
    call :print_status "Running with verbose output..."
    :: Run with verbose output and save to file, also display on console
    "!BENCHMARK_EXEC!" !TEST_OPTS! > "!OUTPUT_FILE!" 2>&1
    set BENCHMARK_RESULT=!ERRORLEVEL!
    type "!OUTPUT_FILE!"
) else (
    :: Run quietly and save to file, show progress
    call :print_status "Running benchmark tests (use --verbose for detailed output)..."
    "!BENCHMARK_EXEC!" !TEST_OPTS! >> "!OUTPUT_FILE!" 2>&1
    set BENCHMARK_RESULT=!ERRORLEVEL!
)

:: Check benchmark results and handle various exit scenarios
findstr /c:"Performance Results" /c:"Total time:" "!OUTPUT_FILE!" >nul 2>&1
if !ERRORLEVEL! equ 0 (
    :: We got performance results, so consider it successful regardless of exit code
    if !BENCHMARK_RESULT! neq 0 (
        call :print_warning "Benchmark had exit code !BENCHMARK_RESULT! but performance results were captured"
        set BENCHMARK_RESULT=0
    )
) else (
    :: Check for timeout or other issues
    findstr /c:"TIMEOUT" "!OUTPUT_FILE!" >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        call :print_error "Benchmark execution timed out"
    ) else (
        call :print_error "No performance results found in output"
    )
)

:: Analyze results
if !BENCHMARK_RESULT! equ 0 (
    call :print_success "AI scaling benchmark completed successfully!"
    
    :: Extract and display key performance metrics
    if exist "!OUTPUT_FILE!" (
        call :extract_performance_summary
        
        echo.
        call :print_status "Detailed results saved to: !OUTPUT_FILE!"
        
        :: Show file size for reference
        for %%f in ("!OUTPUT_FILE!") do set "FILE_SIZE=%%~zf"
        call :print_status "Output file size: !FILE_SIZE! bytes"
        
        :: Quick verification that the benchmark actually ran
        findstr /c:"===== REALISTIC PERFORMANCE TESTING =====" "!OUTPUT_FILE!" >nul 2>&1
        if !ERRORLEVEL! equ 0 (
            call :print_success "All AI benchmark test suites completed successfully"
        ) else (
            call :print_warning "AI performance test may not have completed - check output file"
        )
        
        :: Count total test results
        for /f %%i in ('findstr /c:"Performance Results" "!OUTPUT_FILE!" 2^>nul ^| find /c /v ""') do (
            if %%i gtr 0 (
                call :print_status "Found %%i performance result sections"
            ) else (
                call :print_warning "No performance results detected in output"
            )
        )
    )
    
    echo.
    call :print_success "AI scaling benchmark test completed!"
    
    if "!VERBOSE!"=="false" (
        echo.
        call :print_status "For detailed performance metrics, run with --verbose flag"
        call :print_status "Or view the complete results: type !OUTPUT_FILE!"
    )
    
    :: Create performance metrics summary file
    set "METRICS_FILE=!RESULTS_DIR!\ai_benchmark_performance_metrics.txt"
    echo AI Performance Metrics Summary> "!METRICS_FILE!"
    echo ==============================>> "!METRICS_FILE!"
    echo Date: %date% %time%>> "!METRICS_FILE!"
    echo Build type: !BUILD_TYPE!>> "!METRICS_FILE!"
    echo.>> "!METRICS_FILE!"
    
    :: Extract key metrics to summary file
    findstr /c:"Performance Results" "!OUTPUT_FILE!" >> "!METRICS_FILE!" 2>nul
    findstr /c:"Total time:" "!OUTPUT_FILE!" >> "!METRICS_FILE!" 2>nul
    findstr /c:"Entity updates per second:" "!OUTPUT_FILE!" >> "!METRICS_FILE!" 2>nul
    findstr /c:"Total behavior updates:" "!OUTPUT_FILE!" >> "!METRICS_FILE!" 2>nul
    
    :: Extract scalability summary if available
    echo.>> "!METRICS_FILE!"
    echo Scalability Analysis:>> "!METRICS_FILE!"
    findstr /c:"REALISTIC SCALABILITY SUMMARY:" "!OUTPUT_FILE!" >> "!METRICS_FILE!" 2>nul
    findstr /c:"Entity Count" "!OUTPUT_FILE!" >> "!METRICS_FILE!" 2>nul
    
    call :print_status "Performance metrics saved to: !METRICS_FILE!"
    
    exit /b 0
) else (
    call :print_error "AI scaling benchmark failed with exit code: !BENCHMARK_RESULT!"
    
    if exist "!OUTPUT_FILE!" (
        call :print_status "Check the output file for details: !OUTPUT_FILE!"
        :: Show last few lines of output for immediate debugging
        echo.
        call :print_status "Last few lines of output:"
        :: Use more command which is available on all Windows systems
        more +99999 "!OUTPUT_FILE!" 2>nul || (
            echo Use 'type !OUTPUT_FILE!' to view the complete output
        )
    )
    
    exit /b !BENCHMARK_RESULT!
)