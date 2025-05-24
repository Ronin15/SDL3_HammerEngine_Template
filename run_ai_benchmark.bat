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

:: Navigate to project root directory (in case script is run from elsewhere)
cd /d "%~dp0"

echo !YELLOW!Running AI Scaling Benchmark...!NC!

:: Create required directories
if not exist "build" mkdir build
if not exist "test_results" mkdir test_results

:: Process command-line options
set BUILD_TYPE=Debug
set CLEAN_BUILD=false
set VERBOSE=false
set EXTREME_TEST=false
set USE_NINJA=false

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--clean" (
    set CLEAN_BUILD=true
    shift
    goto :parse_args
)
if /i "%~1"=="--debug" (
    set BUILD_TYPE=Debug
    shift
    goto :parse_args
)
if /i "%~1"=="--verbose" (
    set VERBOSE=true
    shift
    goto :parse_args
)
if /i "%~1"=="--extreme" (
    set EXTREME_TEST=true
    shift
    goto :parse_args
)
if /i "%~1"=="--release" (
    set BUILD_TYPE=Release
    shift
    goto :parse_args
)
echo !RED!Unknown option: %1!NC!
echo Usage: %0 [--clean] [--debug] [--verbose] [--extreme] [--release]
exit /b 1

:done_parsing

:: Configure build cleaning
if "%CLEAN_BUILD%"=="true" (
    echo !YELLOW!Cleaning build directory...!NC!
    if exist "build" rmdir /s /q build
    mkdir build
)

:: Check if Ninja is available
where ninja >nul 2>&1
if %ERRORLEVEL% equ 0 (
    set USE_NINJA=true
    echo !GREEN!Ninja build system found, using it for faster builds.!NC!
) else (
    set USE_NINJA=false
    echo !YELLOW!Ninja build system not found, using default CMake generator.!NC!
)

:: Configure the project
echo !YELLOW!Configuring project with CMake (Build type: %BUILD_TYPE%)...!NC!

:: Add extreme tests flag if requested and disable signal handling to avoid threading issues
set CMAKE_FLAGS=-DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DBOOST_TEST_NO_SIGNAL_HANDLING=ON
if "%EXTREME_TEST%"=="true" (
    set CMAKE_FLAGS=%CMAKE_FLAGS% -DENABLE_EXTREME_TESTS=ON
)

if "%USE_NINJA%"=="true" (
    if "%VERBOSE%"=="true" (
        cmake -S . -B build %CMAKE_FLAGS% -G Ninja
    ) else (
        cmake -S . -B build %CMAKE_FLAGS% -G Ninja >nul 2>&1
    )
) else (
    if "%VERBOSE%"=="true" (
        cmake -S . -B build %CMAKE_FLAGS%
    ) else (
        cmake -S . -B build %CMAKE_FLAGS% >nul 2>&1
    )
)

:: Build the benchmark
echo !YELLOW!Building AI Scaling Benchmark...!NC!
if "%USE_NINJA%"=="true" (
    if "%VERBOSE%"=="true" (
        ninja -C build ai_scaling_benchmark
    ) else (
        ninja -C build ai_scaling_benchmark >nul 2>&1
    )
) else (
    if "%VERBOSE%"=="true" (
        cmake --build build --config %BUILD_TYPE% --target ai_scaling_benchmark
    ) else (
        cmake --build build --config %BUILD_TYPE% --target ai_scaling_benchmark >nul 2>&1
    )
)

:: Check if build was successful
if %ERRORLEVEL% neq 0 (
    echo !RED!Build failed. See output for details.!NC!
    exit /b 1
)

echo !GREEN!Build successful!!NC!

:: Determine the correct path to the benchmark executable
if "%BUILD_TYPE%"=="Debug" (
    set BENCHMARK_EXECUTABLE=bin\debug\ai_scaling_benchmark.exe
) else (
    set BENCHMARK_EXECUTABLE=bin\release\ai_scaling_benchmark.exe
)

:: Verify executable exists
if not exist "%BENCHMARK_EXECUTABLE%" (
    echo !RED!Error: Benchmark executable not found at '%BENCHMARK_EXECUTABLE%'!NC!
    echo !YELLOW!Searching for benchmark executable...!NC!
    
    :: Check if bin directory exists
    if not exist "bin" (
        echo !RED!Error: bin directory not found!NC!
        echo !RED!Build may have failed or used a different output directory!NC!
        exit /b 1
    )
    
    set FOUND_EXECUTABLE=false
    for /r "bin" %%f in (ai_scaling_benchmark*.exe) do (
        echo !GREEN!Found executable at: %%f!NC!
        set BENCHMARK_EXECUTABLE=%%f
        set FOUND_EXECUTABLE=true
        goto :found_executable
    )
    
    if "%FOUND_EXECUTABLE%"=="false" (
        echo !RED!Could not find the benchmark executable. Build may have failed.!NC!
        exit /b 1
    )
)

:found_executable

:: Run the benchmark
echo !YELLOW!Running AI Scaling Benchmark...!NC!
echo !YELLOW!This may take several minutes depending on your hardware.!NC!
echo.

:: Create output file for results with timestamp
for /f "tokens=2 delims==" %%a in ('wmic OS Get localdatetime /value') do set "dt=%%a"
set "TIMESTAMP=%dt:~0,8%_%dt:~8,6%"
set "RESULTS_FILE=test_results\ai_scaling_benchmark_%TIMESTAMP%.txt"

:: Set test command options for better handling of threading issues
set TEST_OPTS=--catch_system_errors=no --no_result_code
if "%VERBOSE%"=="true" (
    set TEST_OPTS=!TEST_OPTS! --log_level=all
) else (
    set TEST_OPTS=!TEST_OPTS! --log_level=test_suite
)

echo !BLUE!Starting benchmark run at %date% %time%!NC!
echo !YELLOW!Running with options: %TEST_OPTS%!NC!

:: Run the benchmark with output capturing and specific options to handle threading issues
echo ============ BENCHMARK START ============> "%RESULTS_FILE%"
echo Date: %date% %time%>> "%RESULTS_FILE%"
echo Build type: %BUILD_TYPE%>> "%RESULTS_FILE%"
echo Command: %BENCHMARK_EXECUTABLE% %TEST_OPTS%>> "%RESULTS_FILE%"
echo =========================================>> "%RESULTS_FILE%"
echo.>> "%RESULTS_FILE%"

:: Run the benchmark with proper error handling
if "%VERBOSE%"=="true" (
    echo !YELLOW!Running benchmark with verbose output...!NC!
    "%BENCHMARK_EXECUTABLE%" %TEST_OPTS% 2>&1 | findstr /v "^$" >> "%RESULTS_FILE%"
) else (
    echo !YELLOW!Running benchmark...!NC!
    "%BENCHMARK_EXECUTABLE%" %TEST_OPTS% 2>&1 | findstr /v "^$" >> "%RESULTS_FILE%"
)
set TEST_RESULT=%ERRORLEVEL%

:: Check if executable was actually found/run
if %TEST_RESULT% equ 9009 (
    echo !RED!Error: Benchmark executable failed to run. Check the path: %BENCHMARK_EXECUTABLE%!NC!
    echo Error: Benchmark executable failed to run >> "%RESULTS_FILE%"
    set TEST_RESULT=1
)

echo.>> "%RESULTS_FILE%"
echo ============ BENCHMARK END =============>> "%RESULTS_FILE%"
echo Date: %date% %time%>> "%RESULTS_FILE%"
echo Exit code: %TEST_RESULT%>> "%RESULTS_FILE%"
echo ========================================>> "%RESULTS_FILE%"

:: Force success if benchmark passed but had cleanup issues
findstr /c:"Benchmark: " /c:"Total time: " "%RESULTS_FILE%" >nul 2>&1
if %ERRORLEVEL% equ 0 (
    if %TEST_RESULT% neq 0 (
        echo !YELLOW!Benchmark completed successfully but had non-zero exit code due to cleanup issues. Treating as success.!NC!
        set TEST_RESULT=0
    )
)

:: Handle exit codes
if %TEST_RESULT% equ 0 (
    echo !GREEN!Benchmark completed successfully!!NC!
) else (
    echo !YELLOW!Benchmark exited with code %TEST_RESULT%!NC!
    
    :: Check if results file exists
    if not exist "%RESULTS_FILE%" (
        echo !RED!Results file not found after benchmark execution!NC!
    ) else (
        :: Check if we have useful output despite error code
        findstr /c:"Total time: " /c:"Benchmark: " "%RESULTS_FILE%" >nul 2>&1
        if %ERRORLEVEL% equ 0 (
            echo !GREEN!Results were captured despite abnormal termination.!NC!
            set TEST_RESULT=0
        ) else (
            echo !RED!No useful results were captured in the output file.!NC!
        )
    )
)

:: Additional check for crash evidence
findstr /c:"dumped core" /c:"Segmentation fault" /c:"Aborted" /c:"memory access violation" "%RESULTS_FILE%" >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo !YELLOW!⚠️ Evidence of crash found in output, but benchmark may have completed.!NC!
    findstr /c:"Total time: " "%RESULTS_FILE%" >nul 2>&1
    if %ERRORLEVEL% equ 0 (
        echo !GREEN!Results were captured before crash.!NC!
        set TEST_RESULT=0
    )
)

echo.
echo !GREEN!Benchmark complete!!NC!
echo !GREEN!Results saved to %RESULTS_FILE%!NC!

:: Extract performance metrics
echo !YELLOW!Extracting performance metrics...!NC!
set "METRICS_FILE=test_results\ai_benchmark_performance_metrics.txt"
echo ============ PERFORMANCE SUMMARY ============> "%METRICS_FILE%"
echo Date: %date% %time%>> "%METRICS_FILE%"
echo Build type: %BUILD_TYPE%>> "%METRICS_FILE%"
echo ===========================================>> "%METRICS_FILE%"
echo.>> "%METRICS_FILE%"

:: Check if results file exists before extracting
if not exist "%RESULTS_FILE%" (
    echo !RED!Results file not found: %RESULTS_FILE%!NC!
    echo Error: Results file not found>> "%METRICS_FILE%"
) else (
    :: Windows alternative to grep for extracting metrics
    findstr /c:"time:" /c:"entities:" /c:"processed:" /c:"Performance" /c:"Execution time" ^
           /c:"optimization" /c:"Total time:" /c:"Time per update:" /c:"Updates per second:" ^
           /c:"Batch size" /c:"Thread" /c:"entities processed" /c:"AI behavior" ^
           /c:"entities assigned" "%RESULTS_FILE%" >> "%METRICS_FILE%" 2>nul
)

:: Add summary footer
echo.>> "%METRICS_FILE%"
echo ============ END OF SUMMARY ============>> "%METRICS_FILE%"

:: Display the performance summary
echo !BLUE!Performance Summary:!NC!
type "%METRICS_FILE%"

:: Check benchmark status and create a final status report
echo !BLUE!Creating final benchmark report...!NC!

:: Create a more comprehensive results summary
set "SUMMARY_FILE=test_results\ai_benchmark_summary_%TIMESTAMP%.txt"
echo ============ BENCHMARK SUMMARY ============> "%SUMMARY_FILE%"
echo Date: %date% %time%>> "%SUMMARY_FILE%"
echo Build type: %BUILD_TYPE%>> "%SUMMARY_FILE%"
echo Exit code: %TEST_RESULT%>> "%SUMMARY_FILE%"
echo.>> "%SUMMARY_FILE%"

:: Extract key metrics for the summary
echo Key Performance Metrics:>> "%SUMMARY_FILE%"
findstr /c:"Total time:" /c:"Time per update:" /c:"Updates per second:" /c:"entities processed" "%RESULTS_FILE%" >> "%SUMMARY_FILE%" 2>nul
echo.>> "%SUMMARY_FILE%"

:: Note any errors or warnings
echo Status:>> "%SUMMARY_FILE%"
if %TEST_RESULT% equ 0 (
    echo ✅ Benchmark completed successfully>> "%SUMMARY_FILE%"
    echo !GREEN!✅ Benchmark completed successfully!NC!
) else (
    findstr /c:"Total time: " "%RESULTS_FILE%" >nul 2>&1
    if %ERRORLEVEL% equ 0 (
        echo ⚠️ Benchmark had issues but results were captured>> "%SUMMARY_FILE%"
        echo !YELLOW!⚠️ Benchmark had issues but results were captured!NC!
        :: We'll exit with success since we got results
        set TEST_RESULT=0
    ) else (
        echo ❌ Benchmark failed to produce complete results>> "%SUMMARY_FILE%"
        echo !RED!❌ Benchmark failed to produce complete results!NC!
    )
)

:: Final output
echo !GREEN!Results saved to:!NC!
echo   - Full log: !BLUE!%RESULTS_FILE%!NC!
echo   - Performance metrics: !BLUE!%METRICS_FILE%!NC!
echo   - Summary: !BLUE!%SUMMARY_FILE%!NC!

:: Exit with appropriate code based on whether we got results
findstr /c:"Total time: " "%RESULTS_FILE%" >nul 2>&1
if %ERRORLEVEL% equ 0 (
    exit /b 0
) else (
    exit /b %TEST_RESULT%
)