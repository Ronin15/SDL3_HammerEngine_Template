@echo off
:: Script to run the AI Optimization Tests
:: Copyright (c) 2025 Hammer Forged Games, MIT License

echo Running AI Optimization Tests...

:: Create required directories
if not exist "build" mkdir build
if not exist "test_results" mkdir test_results

:: Process command-line options
set BUILD_TYPE=Debug
set CLEAN_BUILD=false
set VERBOSE=false

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
if /i "%~1"=="--release" (
    set BUILD_TYPE=Release
    shift
    goto :parse_args
)
if /i "%~1"=="--verbose" (
    set VERBOSE=true
    shift
    goto :parse_args
)
if /i "%~1"=="--help" (
    echo Usage: %0 [--clean] [--debug] [--release] [--verbose] [--help]
    echo   --clean     Clean build directory before building
    echo   --debug     Build in Debug mode (default)
    echo   --release   Build in Release mode
    echo   --verbose   Show verbose output
    echo   --help      Show this help message
    exit /b 0
)
echo Unknown option: %1
echo Usage: %0 [--clean] [--debug] [--release] [--verbose] [--help]
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
set CMAKE_FLAGS=-DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DBOOST_TEST_NO_SIGNAL_HANDLING=ON

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

:: Build the tests
echo Building AI Optimization tests...
if "%USE_NINJA%"=="true" (
    if "%VERBOSE%"=="true" (
        ninja -C build ai_optimization_tests
    ) else (
        ninja -C build ai_optimization_tests > nul
    )
) else (
    if "%VERBOSE%"=="true" (
        cmake --build build --config %BUILD_TYPE% --target ai_optimization_tests
    ) else (
        cmake --build build --config %BUILD_TYPE% --target ai_optimization_tests > nul
    )
)

:: Check if build was successful
if %ERRORLEVEL% neq 0 (
    echo Build failed. See output for details.
    exit /b 1
)

echo Build successful!

:: Determine the correct path to the test executable
if "%BUILD_TYPE%"=="Debug" (
    set TEST_EXECUTABLE=bin\debug\ai_optimization_tests.exe
) else (
    set TEST_EXECUTABLE=bin\release\ai_optimization_tests.exe
)

:: Verify executable exists
if not exist "%TEST_EXECUTABLE%" (
    echo Error: Test executable not found at '%TEST_EXECUTABLE%'
    echo Searching for test executable...
    for /r "bin" %%f in (ai_optimization_tests*.exe) do (
        echo Found executable at: %%f
        set TEST_EXECUTABLE=%%f
        goto :found_executable
    )
    echo Could not find the test executable. Build may have failed or placed the executable in an unexpected location.
    exit /b 1
)

:found_executable

:: Run tests and save output
echo Running AI Optimization tests...

:: Ensure test_results directory exists
if not exist "test_results" mkdir test_results

:: Output file
set OUTPUT_FILE=test_results\ai_optimization_tests_output.txt
set METRICS_FILE=test_results\ai_optimization_tests_performance_metrics.txt

:: Set test command options
set TEST_OPTS=--log_level=all --catch_system_errors=no
if "%VERBOSE%"=="true" (
    set TEST_OPTS=%TEST_OPTS% --report_level=detailed
)

:: Run the tests
echo Running with options: %TEST_OPTS%
"%TEST_EXECUTABLE%" %TEST_OPTS% > "%OUTPUT_FILE%" 2>&1
set TEST_RESULT=%ERRORLEVEL%

:: Extract performance metrics
echo Extracting performance metrics...
findstr /R /C:"time:" /C:"entities:" /C:"processed:" /C:"Performance" /C:"Execution time" /C:"optimization" "%OUTPUT_FILE%" > "%METRICS_FILE%" 2>nul

:: Check test status
if %TEST_RESULT% neq 0 (
    echo Some tests failed! See %OUTPUT_FILE% for details.
    exit /b 1
) else (
    echo All AI Optimization tests passed!
    echo Test results saved to %OUTPUT_FILE%
    echo Performance metrics saved to %METRICS_FILE%
    exit /b 0
)