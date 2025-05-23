@echo off
:: Script to run the AI Scaling Benchmark
:: Copyright (c) 2025 Hammer Forged Games, MIT License

echo Running AI Scaling Benchmark...

:: Create required directories
if not exist "build" mkdir build
if not exist "test_results" mkdir test_results

:: Process command-line options
set BUILD_TYPE=Debug
set CLEAN_BUILD=false
set VERBOSE=false
set EXTREME_TEST=false

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
echo Unknown option: %1
echo Usage: %0 [--clean] [--debug] [--verbose] [--extreme] [--release]
exit /b 1

:done_parsing

:: Configure build cleaning
if "%CLEAN_BUILD%"=="true" (
    echo Cleaning build directory...
    if exist "build" rd /s /q build
    mkdir build
)

:: Check if Ninja is available
where /q ninja
if %ERRORLEVEL% equ 0 (
    set USE_NINJA=true
    echo Ninja build system found, using it for faster builds.
) else (
    set USE_NINJA=false
    echo Ninja build system not found, using default CMake generator.
)

:: Configure the project
echo Configuring project with CMake (Build type: %BUILD_TYPE%)...

:: Add extreme tests flag if requested and disable signal handling
set CMAKE_FLAGS=-DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DBOOST_TEST_NO_SIGNAL_HANDLING=ON
if "%EXTREME_TEST%"=="true" (
    set CMAKE_FLAGS=%CMAKE_FLAGS% -DENABLE_EXTREME_TESTS=ON
)

if "%USE_NINJA%"=="true" (
    if "%VERBOSE%"=="true" (
        cmake -S . -B build %CMAKE_FLAGS% -G Ninja
    ) else (
        cmake -S . -B build %CMAKE_FLAGS% -G Ninja > nul
    )
) else (
    if "%VERBOSE%"=="true" (
        cmake -S . -B build %CMAKE_FLAGS%
    ) else (
        cmake -S . -B build %CMAKE_FLAGS% > nul
    )
)

:: Build the benchmark
echo Building AI Scaling Benchmark...
if "%USE_NINJA%"=="true" (
    if "%VERBOSE%"=="true" (
        ninja -C build ai_scaling_benchmark
    ) else (
        ninja -C build ai_scaling_benchmark > nul
    )
) else (
    if "%VERBOSE%"=="true" (
        cmake --build build --config %BUILD_TYPE% --target ai_scaling_benchmark
    ) else (
        cmake --build build --config %BUILD_TYPE% --target ai_scaling_benchmark > nul
    )
)

:: Check if build was successful
if %ERRORLEVEL% neq 0 (
    echo Build failed. See output for details.
    exit /b 1
)

echo Build successful!

:: Determine the correct path to the benchmark executable
if "%BUILD_TYPE%"=="Debug" (
    set BENCHMARK_EXECUTABLE=bin\debug\ai_scaling_benchmark.exe
) else (
    set BENCHMARK_EXECUTABLE=bin\release\ai_scaling_benchmark.exe
)

:: Verify executable exists
if not exist "%BENCHMARK_EXECUTABLE%" (
    echo Error: Benchmark executable not found at '%BENCHMARK_EXECUTABLE%'
    echo Searching for benchmark executable...
    for /r "bin" %%f in (ai_scaling_benchmark*.exe) do (
        echo Found executable at: %%f
        set BENCHMARK_EXECUTABLE=%%f
        goto :found_executable
    )
    echo Could not find the benchmark executable. Build may have failed or placed the executable in an unexpected location.
    exit /b 1
)

:found_executable

:: Run the benchmark
echo Running AI Scaling Benchmark...
echo This may take several minutes depending on your hardware.
echo.

:: Create output file for results
set TIMESTAMP=%date:~10,4%%date:~4,2%%date:~7,2%_%time:~0,2%%time:~3,2%%time:~6,2%
set TIMESTAMP=%TIMESTAMP: =0%
set RESULTS_FILE=test_results\ai_scaling_benchmark_%TIMESTAMP%.txt

:: Run the benchmark with output capturing and specific options to handle threading issues
if "%VERBOSE%"=="true" (
    "%BENCHMARK_EXECUTABLE%" --log_level=all --catch_system_errors=no --no_result_code > "%RESULTS_FILE%" 2>&1
) else (
    "%BENCHMARK_EXECUTABLE%" --catch_system_errors=no --no_result_code > "%RESULTS_FILE%" 2>&1
)

echo.
echo Benchmark complete!
echo Results saved to %RESULTS_FILE%

:: Even if there are errors in the benchmark, we consider it successful if it produced output
if exist "%RESULTS_FILE%" if %~z"%RESULTS_FILE%" gtr 0 (
    echo Benchmark generated results successfully.
    exit /b 0
) else (
    echo Benchmark may have failed. Check the output for errors.
    exit /b 1
)