@echo off
:: EventManager Scaling Benchmark Test Script
:: Copyright (c) 2025 Hammer Forged Games
:: Licensed under the MIT License

setlocal EnableDelayedExpansion

:: Colors for output
set "RED=[91m"
set "GREEN=[92m"
set "YELLOW=[93m"
set "BLUE=[94m"
set "NC=[0m"

:: Script configuration
set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%SCRIPT_DIR%..\..\build"
set "RESULTS_DIR=%SCRIPT_DIR%..\..\test_results"
set "OUTPUT_FILE=%RESULTS_DIR%\event_scaling_benchmark_output.txt"
set "BUILD_TYPE=debug"
set "VERBOSE=false"

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
echo EventManager Scaling Benchmark Test Script
echo.
echo Usage: run_event_scaling_benchmark.bat [options]
echo.
echo Options:
echo   --release     Run benchmark in release mode ^(optimized^)
echo   --verbose     Show detailed benchmark output
echo   --clean       Clean build artifacts before building
echo   --help        Show this help message
echo.
echo Examples:
echo   run_event_scaling_benchmark.bat                    # Run benchmark with default settings
echo   run_event_scaling_benchmark.bat --release         # Run optimized benchmark
echo   run_event_scaling_benchmark.bat --verbose         # Show detailed performance metrics
echo   run_event_scaling_benchmark.bat --clean --release # Clean build and run optimized benchmark
echo.
echo Output:
echo   Results are saved to: !OUTPUT_FILE!
echo   Console output shows real-time benchmark progress
echo.
echo The benchmark tests EventManager performance across multiple scales:
echo   - Basic handler performance ^(small scale^)
echo   - Medium scale performance ^(5K events, 25K handlers^)
echo   - Comprehensive scalability suite
echo   - Concurrency testing ^(multi-threaded^)
echo   - Extreme scale testing ^(100K events, 5M handlers^)
goto :eof

:extract_performance_summary
:: Extract key performance metrics from the benchmark output
call :print_status "Extracting performance summary..."

echo.
echo !BLUE!==== PERFORMANCE SUMMARY ====!NC!

:: Check if extreme scale test exists
findstr /c:"===== EXTREME SCALE TEST =====" "!OUTPUT_FILE!" >nul 2>&1
if !ERRORLEVEL! equ 0 (
    echo.
    echo !YELLOW!Extreme Scale Test Results:!NC!
    :: Extract lines after extreme scale test
    for /f "tokens=*" %%a in ('findstr /c:"Events per second:" "!OUTPUT_FILE!" 2^>nul') do (
        echo   %%a
    )
) else (
    call :print_warning "Extreme scale test section not found in output"
)

:: Extract performance metrics
echo.
echo !YELLOW!Performance Metrics Found:!NC!
findstr /c:"Total time:" "!OUTPUT_FILE!" 2>nul
findstr /c:"Events per second:" "!OUTPUT_FILE!" 2>nul
findstr /c:"Handler calls per second:" "!OUTPUT_FILE!" 2>nul

:: Check for test verification (using alternative characters that work in Windows)
echo.
echo !YELLOW!Test Verification:!NC!
findstr /c:"Handler calls:" "!OUTPUT_FILE!" | findstr /c:"/" >nul 2>&1
if !ERRORLEVEL! equ 0 (
    echo !GREEN!  Handler verification tests found and checked!NC!
    :: Count successful vs failed verifications
    for /f %%i in ('findstr /c:"Handler calls:" "!OUTPUT_FILE!" 2^>nul ^| find /c /v ""') do (
        echo    Total handler verification checks: %%i
    )
) else (
    echo !YELLOW!  No handler verification data found!NC!
)

:: Check for failures by looking for mismatch patterns
findstr /c:"Handler calls:" "!OUTPUT_FILE!" | findstr /v /c:"/" >nul 2>&1
if !ERRORLEVEL! equ 0 (
    echo !RED!  Warning: Handler verification issues detected!NC!
) else (
    echo !GREEN!  All handler verifications appear successful!NC!
)

echo.
goto :eof

:main
:: Navigate to script directory
cd /d "%SCRIPT_DIR%"

:: Parse command line arguments
:parse_args
if "%~1"=="" goto :done_parsing
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

call :print_status "Starting EventManager Scaling Benchmark..."
call :print_status "Build type: !BUILD_TYPE!"
call :print_status "Results will be saved to: !OUTPUT_FILE!"

:: Navigate to script directory
cd /d "%SCRIPT_DIR%"

:: Check if benchmark executable exists
set "BENCHMARK_EXEC=!SCRIPT_DIR!..\..\bin\!BUILD_TYPE!\event_manager_scaling_benchmark.exe"
if not exist "!BENCHMARK_EXEC!" (
    set "BENCHMARK_EXEC=!SCRIPT_DIR!..\..\bin\!BUILD_TYPE!\event_manager_scaling_benchmark"
    if not exist "!BENCHMARK_EXEC!" (
        call :print_error "Benchmark executable not found: !BENCHMARK_EXEC!"
        call :print_status "Please build the project first using the build script"
        exit /b 1
    )
)

:: Prepare output file
echo EventManager Scaling Benchmark Results> "!OUTPUT_FILE!"
echo =======================================>> "!OUTPUT_FILE!"
echo Date: %date% %time%>> "!OUTPUT_FILE!"
echo Build Type: !BUILD_TYPE!>> "!OUTPUT_FILE!"
echo System: %OS% %PROCESSOR_ARCHITECTURE%>> "!OUTPUT_FILE!"
echo.>> "!OUTPUT_FILE!"

:: Run the benchmark
call :print_status "Running EventManager scaling benchmark..."
call :print_status "This may take several minutes for comprehensive testing..."

if "!VERBOSE!"=="true" (
    call :print_status "Running with verbose output..."
    :: Run with verbose output and save to file, also display on console
    "!BENCHMARK_EXEC!" > "!OUTPUT_FILE!" 2>&1
    set BENCHMARK_RESULT=!ERRORLEVEL!
    type "!OUTPUT_FILE!"
) else (
    :: Run quietly and save to file, show progress
    call :print_status "Running benchmark tests (use --verbose for detailed output)..."
    "!BENCHMARK_EXEC!" >> "!OUTPUT_FILE!" 2>&1
    set BENCHMARK_RESULT=!ERRORLEVEL!
)

:: Check benchmark results
if !BENCHMARK_RESULT! equ 0 (
    call :print_success "EventManager scaling benchmark completed successfully!"
    
    :: Extract and display key performance metrics
    if exist "!OUTPUT_FILE!" (
        call :extract_performance_summary
        
        echo.
        call :print_status "Detailed results saved to: !OUTPUT_FILE!"
        
        :: Show file size for reference
        for %%f in ("!OUTPUT_FILE!") do set "FILE_SIZE=%%~zf"
        call :print_status "Output file size: !FILE_SIZE! bytes"
        
        :: Quick verification that the benchmark actually ran
        findstr /c:"===== EXTREME SCALE TEST =====" "!OUTPUT_FILE!" >nul 2>&1
        if !ERRORLEVEL! equ 0 (
            call :print_success "All benchmark test suites completed successfully"
        ) else (
            call :print_warning "Extreme scale test may not have completed - check output file"
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
    call :print_success "EventManager scaling benchmark test completed!"
    
    if "!VERBOSE!"=="false" (
        echo.
        call :print_status "For detailed performance metrics, run with --verbose flag"
        call :print_status "Or view the complete results: type !OUTPUT_FILE!"
    )
    
    exit /b 0
) else (
    call :print_error "EventManager scaling benchmark failed with exit code: !BENCHMARK_RESULT!"
    
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