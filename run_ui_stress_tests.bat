@echo off
REM UI Stress Test Runner Script for Windows
REM Copyright (c) 2025 Hammer Forged Games
REM All rights reserved.
REM Licensed under the MIT License - see LICENSE file for details

setlocal enabledelayedexpansion

REM Script configuration
set BUILD_DIR=build
set TEST_EXECUTABLE=bin\debug\ui_stress_test.exe
set LOG_DIR=test_results\ui_stress
set TIMESTAMP=%date:~-4,4%%date:~-10,2%%date:~-7,2%_%time:~0,2%%time:~3,2%%time:~6,2%
set TIMESTAMP=!TIMESTAMP: =0!
set LOG_FILE=%LOG_DIR%\ui_stress_test_!TIMESTAMP!.log

REM Test configuration options
set STRESS_LEVEL=medium
set TEST_DURATION=30
set MAX_COMPONENTS=500
set ENABLE_MEMORY_STRESS=false
set TEST_RESOLUTIONS=true
set TEST_PRESENTATION_MODES=true
set VERBOSE=false
set SAVE_RESULTS=true
set BENCHMARK_MODE=false

REM Colors for output (Windows 10+ with ANSI support)
set RED=[91m
set GREEN=[92m
set YELLOW=[93m
set BLUE=[94m
set NC=[0m

REM Function to print colored output
goto :main

:print_status
echo %BLUE%[INFO]%NC% %~1
goto :eof

:print_success
echo %GREEN%[SUCCESS]%NC% %~1
goto :eof

:print_warning
echo %YELLOW%[WARNING]%NC% %~1
goto :eof

:print_error
echo %RED%[ERROR]%NC% %~1
goto :eof

:show_usage
echo UI Stress Test Runner for Windows
echo.
echo Usage: %0 [OPTIONS]
echo.
echo Options:
echo   /l LEVEL        Stress test level (light^|medium^|heavy^|extreme) [default: medium]
echo   /d SECONDS      Test duration in seconds [default: 30]
echo   /c COUNT        Maximum components to create [default: 500]
echo   /m              Enable memory pressure testing [default: false]
echo   /r              Skip resolution scaling tests [default: false]
echo   /p              Skip presentation mode tests [default: false]
echo   /v              Enable verbose output [default: false]
echo   /s              Save results to file [default: true]
echo   /b              Run benchmark suite instead of stress tests [default: false]
echo   /h              Show this help message
echo.
echo Examples:
echo   %0                          # Run medium stress test with defaults
echo   %0 /l heavy /d 60           # Run heavy stress test for 60 seconds
echo   %0 /b                       # Run benchmark suite
echo   %0 /l light /m              # Run light test with memory pressure
echo.
goto :eof

:parse_arguments
:parse_loop
if "%~1"=="" goto :parse_done
if "%~1"=="/l" (
    set STRESS_LEVEL=%~2
    shift
    shift
    goto :parse_loop
)
if "%~1"=="/d" (
    set TEST_DURATION=%~2
    shift
    shift
    goto :parse_loop
)
if "%~1"=="/c" (
    set MAX_COMPONENTS=%~2
    shift
    shift
    goto :parse_loop
)
if "%~1"=="/m" (
    set ENABLE_MEMORY_STRESS=true
    shift
    goto :parse_loop
)
if "%~1"=="/r" (
    set TEST_RESOLUTIONS=false
    shift
    goto :parse_loop
)
if "%~1"=="/p" (
    set TEST_PRESENTATION_MODES=false
    shift
    goto :parse_loop
)
if "%~1"=="/v" (
    set VERBOSE=true
    shift
    goto :parse_loop
)
if "%~1"=="/s" (
    set SAVE_RESULTS=true
    shift
    goto :parse_loop
)
if "%~1"=="/b" (
    set BENCHMARK_MODE=true
    shift
    goto :parse_loop
)
if "%~1"=="/h" (
    call :show_usage
    exit /b 0
)
call :print_error "Unknown option: %~1"
call :show_usage
exit /b 1

:parse_done
goto :eof

:validate_stress_level
if "%STRESS_LEVEL%"=="light" goto :validate_ok
if "%STRESS_LEVEL%"=="medium" goto :validate_ok
if "%STRESS_LEVEL%"=="heavy" goto :validate_ok
if "%STRESS_LEVEL%"=="extreme" goto :validate_ok
call :print_error "Invalid stress level: %STRESS_LEVEL%"
call :print_error "Valid levels: light, medium, heavy, extreme"
exit /b 1
:validate_ok
goto :eof

:check_prerequisites
call :print_status "Checking prerequisites..."

REM Check if build directory exists
if not exist "%BUILD_DIR%" (
    call :print_error "Build directory not found: %BUILD_DIR%"
    call :print_error "Please run cmake and build the project first"
    exit /b 1
)

REM Check if test executable exists
if not exist "%BUILD_DIR%\%TEST_EXECUTABLE%" (
    call :print_error "Test executable not found: %BUILD_DIR%\%TEST_EXECUTABLE%"
    call :print_error "Please build the project first (make sure UI stress test target is built)"
    exit /b 1
)

REM Create log directory if it doesn't exist
if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"

call :print_success "Prerequisites check passed"
goto :eof

:build_project
call :print_status "Building project..."

REM Change to build directory
pushd "%BUILD_DIR%"

REM Build the project using MSBuild or make
if exist "*.sln" (
    REM Visual Studio solution found
    msbuild /p:Configuration=Release > build.log 2>&1
) else if exist "Makefile" (
    REM Makefile found
    make -j%NUMBER_OF_PROCESSORS% > build.log 2>&1
) else (
    call :print_error "No build system found (no .sln or Makefile)"
    popd
    exit /b 1
)

if !errorlevel! equ 0 (
    call :print_success "Project built successfully"
) else (
    call :print_error "Build failed. Check %BUILD_DIR%\build.log for details"
    popd
    exit /b 1
)

REM Return to original directory
popd
goto :eof

:run_stress_tests
call :print_status "Starting UI stress tests..."
call :print_status "Test Level: %STRESS_LEVEL%"
call :print_status "Duration: %TEST_DURATION%s"
call :print_status "Max Components: %MAX_COMPONENTS%"
call :print_status "Memory Stress: %ENABLE_MEMORY_STRESS%"
call :print_status "Test Resolutions: %TEST_RESOLUTIONS%"
call :print_status "Test Presentation Modes: %TEST_PRESENTATION_MODES%"

REM Prepare test arguments
set TEST_ARGS=--stress-level %STRESS_LEVEL%
set TEST_ARGS=%TEST_ARGS% --duration %TEST_DURATION%
set TEST_ARGS=%TEST_ARGS% --max-components %MAX_COMPONENTS%

if "%ENABLE_MEMORY_STRESS%"=="true" (
    set TEST_ARGS=%TEST_ARGS% --memory-stress
)

if "%TEST_RESOLUTIONS%"=="false" (
    set TEST_ARGS=%TEST_ARGS% --skip-resolutions
)

if "%TEST_PRESENTATION_MODES%"=="false" (
    set TEST_ARGS=%TEST_ARGS% --skip-presentation
)

if "%VERBOSE%"=="true" (
    set TEST_ARGS=%TEST_ARGS% --verbose
)

if "%SAVE_RESULTS%"=="true" (
    set TEST_ARGS=%TEST_ARGS% --save-results %LOG_FILE%
)

REM Run the tests
call :print_status "Executing: %BUILD_DIR%\%TEST_EXECUTABLE% %TEST_ARGS%"

if "%VERBOSE%"=="true" (
    REM Run with output to both console and log file
    "%BUILD_DIR%\%TEST_EXECUTABLE%" %TEST_ARGS% 2>&1 | tee "%LOG_FILE%"
    set TEST_RESULT=!errorlevel!
) else (
    REM Run with output only to log file
    "%BUILD_DIR%\%TEST_EXECUTABLE%" %TEST_ARGS% > "%LOG_FILE%" 2>&1
    set TEST_RESULT=!errorlevel!
)

if !TEST_RESULT! equ 0 (
    call :print_success "UI stress tests completed successfully"
) else (
    call :print_error "UI stress tests failed (exit code: !TEST_RESULT!)"
    call :print_error "Check log file for details: %LOG_FILE%"
    exit /b 1
)
goto :eof

:run_benchmark_suite
call :print_status "Starting UI benchmark suite..."

REM Prepare benchmark arguments
set BENCHMARK_ARGS=--benchmark

if "%VERBOSE%"=="true" (
    set BENCHMARK_ARGS=!BENCHMARK_ARGS! --verbose
)

if "%SAVE_RESULTS%"=="true" (
    set BENCHMARK_ARGS=!BENCHMARK_ARGS! --save-results %LOG_FILE%
)

REM Run the benchmarks
call :print_status "Executing: %BUILD_DIR%\%TEST_EXECUTABLE% %BENCHMARK_ARGS%"

if "%VERBOSE%"=="true" (
    REM Run with output to both console and log file
    "%BUILD_DIR%\%TEST_EXECUTABLE%" !BENCHMARK_ARGS! 2>&1 | tee "%LOG_FILE%"
    set BENCHMARK_RESULT=!errorlevel!
) else (
    REM Run with output only to log file
    "%BUILD_DIR%\%TEST_EXECUTABLE%" !BENCHMARK_ARGS! > "%LOG_FILE%" 2>&1
    set BENCHMARK_RESULT=!errorlevel!
)

if !BENCHMARK_RESULT! equ 0 (
    call :print_success "UI benchmark suite completed successfully"
) else (
    call :print_error "UI benchmark suite failed (exit code: !BENCHMARK_RESULT!)"
    call :print_error "Check log file for details: %LOG_FILE%"
    exit /b 1
)
goto :eof

:display_results_summary
if exist "%LOG_FILE%" (
    call :print_status "Results Summary:"
    echo.
    
    REM Extract key metrics from log file
    findstr /C:"=== UI Stress Test Results ===" "%LOG_FILE%" >nul
    if !errorlevel! equ 0 (
        echo Test Results Found:
        REM Show first 16 lines after the results header
        for /f "skip=1 tokens=*" %%a in ('findstr /n /C:"=== UI Stress Test Results ===" "%LOG_FILE%"') do (
            set LINE_NUM=%%a
            goto :show_results
        )
    ) else (
        findstr /C:"=== UI Performance Benchmark Results ===" "%LOG_FILE%" >nul
        if !errorlevel! equ 0 (
            echo Benchmark Results Found:
            REM Show first 11 lines after the benchmark header
            for /f "skip=1 tokens=*" %%a in ('findstr /n /C:"=== UI Performance Benchmark Results ===" "%LOG_FILE%"') do (
                set LINE_NUM=%%a
                goto :show_benchmark_results
            )
        ) else (
            call :print_warning "No formatted results found in log file"
        )
    )
    
    :show_results
    REM This is simplified - in a real implementation you'd extract specific lines
    findstr /A:16 "Duration\|Frames\|FPS\|Memory\|Components" "%LOG_FILE%" 2>nul
    goto :results_done
    
    :show_benchmark_results
    REM This is simplified - in a real implementation you'd extract specific lines
    findstr /A:11 "Test Name\|PASS\|FAIL\|FPS" "%LOG_FILE%" 2>nul
    goto :results_done
    
    :results_done
    echo.
    call :print_status "Full results saved to: %LOG_FILE%"
) else (
    call :print_warning "No log file found"
)
goto :eof

:check_system_resources
call :print_status "System Information:"

REM Get CPU info
echo CPU Cores: %NUMBER_OF_PROCESSORS%

REM Get memory info (simplified)
for /f "tokens=2 delims=:" %%a in ('systeminfo ^| findstr /C:"Total Physical Memory"') do (
    echo Total Memory:%%a
)

REM Get GPU info if available
nvidia-smi --query-gpu=name --format=csv,noheader,nounits >nul 2>&1
if !errorlevel! equ 0 (
    for /f "tokens=*" %%a in ('nvidia-smi --query-gpu=name --format=csv,noheader,nounits') do (
        echo GPU: %%a
        goto :gpu_done
    )
)
:gpu_done

echo.
goto :eof

:cleanup_old_results
if exist "%LOG_DIR%" (
    REM Remove test result files older than 7 days
    forfiles /p "%LOG_DIR%" /s /m ui_stress_test_*.log /d -7 /c "cmd /c del @path" 2>nul
    call :print_status "Cleaned up old test results"
)
goto :eof

:main
echo =====================================
echo     UI Stress Test Runner v1.0
echo =====================================
echo.

REM Parse command line arguments
call :parse_arguments %*

REM Validate arguments
call :validate_stress_level
if !errorlevel! neq 0 exit /b !errorlevel!

REM Show system information
call :check_system_resources

REM Check prerequisites
call :check_prerequisites
if !errorlevel! neq 0 exit /b !errorlevel!

REM Clean up old results
call :cleanup_old_results

REM Build project if needed
REM call :build_project
REM if !errorlevel! neq 0 exit /b !errorlevel!

REM Run tests or benchmarks
if "%BENCHMARK_MODE%"=="true" (
    call :run_benchmark_suite
    if !errorlevel! neq 0 (
        call :print_error "Benchmark suite failed"
        exit /b 1
    ) else (
        call :print_success "Benchmark suite completed successfully"
    )
) else (
    call :run_stress_tests
    if !errorlevel! neq 0 (
        call :print_error "Stress tests failed"
        exit /b 1
    ) else (
        call :print_success "Stress tests completed successfully"
    )
)

REM Display results
call :display_results_summary

echo.
call :print_success "UI testing completed!"
echo =====================================

exit /b 0