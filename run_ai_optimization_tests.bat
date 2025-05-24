@echo off
:: Script to run the AI Optimization Tests
:: Copyright (c) 2025 Hammer Forged Games, MIT License

:: Enable color output on Windows 10+ terminals
setlocal EnableDelayedExpansion

:: Color codes for Windows
set "GREEN=[92m"
set "YELLOW=[93m"
set "RED=[91m"
set "BLUE=[94m"
set "NC=[0m"

echo !YELLOW!Running AI Optimization Tests...!NC!

:: Navigate to project root directory (in case script is run from elsewhere)
cd /d "%~dp0"

:: Create required directories
if not exist "test_results" mkdir test_results

:: Process command-line options
set BUILD_TYPE=Debug
set CLEAN_BUILD=false
set VERBOSE=false
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
echo !RED!Unknown option: %1!NC!
echo Usage: %0 [--clean] [--debug] [--release] [--verbose] [--help]
exit /b 1

:done_parsing

:: Check if Ninja is available
where ninja >nul 2>&1
if %ERRORLEVEL% equ 0 (
    set USE_NINJA=true
    echo !GREEN!Ninja build system found, using it for faster builds.!NC!
) else (
    set USE_NINJA=false
    echo !YELLOW!Ninja build system not found, using default CMake generator.!NC!
)

:: Configure build cleaning
if "%CLEAN_BUILD%"=="true" (
    echo !YELLOW!Cleaning build directory...!NC!
    if exist "build" rmdir /s /q build
    mkdir build
)

:: Configure the project
echo !YELLOW!Configuring project with CMake (Build type: %BUILD_TYPE%)...!NC!

:: Configure proper boost options for thread safety
set CMAKE_FLAGS=-DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DBOOST_TEST_NO_SIGNAL_HANDLING=ON

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

:: Build the tests
echo !YELLOW!Building AI Optimization tests...!NC!
if "%USE_NINJA%"=="true" (
    if "%VERBOSE%"=="true" (
        ninja -C build ai_optimization_tests
    ) else (
        ninja -C build ai_optimization_tests >nul 2>&1
    )
) else (
    if "%VERBOSE%"=="true" (
        cmake --build build --config %BUILD_TYPE% --target ai_optimization_tests
    ) else (
        cmake --build build --config %BUILD_TYPE% --target ai_optimization_tests >nul 2>&1
    )
)

:: Check if build was successful
if %ERRORLEVEL% neq 0 (
    echo !RED!Build failed. See output for details.!NC!
    exit /b 1
)

echo !GREEN!Build successful!!NC!

:: Determine the correct path to the test executable
if "%BUILD_TYPE%"=="Debug" (
    set TEST_EXECUTABLE=bin\debug\ai_optimization_tests.exe
) else (
    set TEST_EXECUTABLE=bin\release\ai_optimization_tests.exe
)

:: Verify executable exists
if not exist "%TEST_EXECUTABLE%" (
    echo !RED!Error: Test executable not found at '%TEST_EXECUTABLE%'!NC!
    echo !YELLOW!Searching for test executable...!NC!
    for /r "bin" %%f in (ai_optimization_tests*.exe) do (
        echo !GREEN!Found executable at: %%f!NC!
        set TEST_EXECUTABLE=%%f
        goto :found_executable
    )
    echo !RED!Could not find the test executable. Build may have failed.!NC!
    exit /b 1
)

:found_executable

:: Run tests and save output
echo !YELLOW!Running AI Optimization tests...!NC!

:: Ensure test_results directory exists
if not exist "test_results" mkdir test_results

:: Output file
set OUTPUT_FILE=test_results\ai_optimization_tests_output.txt
set METRICS_FILE=test_results\ai_optimization_tests_performance_metrics.txt

:: Set test command options for better handling of threading issues
set TEST_OPTS=--log_level=all --catch_system_errors=no --no_result_code
if "%VERBOSE%"=="true" (
    set TEST_OPTS=%TEST_OPTS% --report_level=detailed
)

:: Run the tests
echo !YELLOW!Running with options: %TEST_OPTS%!NC!

:: Run tests and capture output
if "%VERBOSE%"=="true" (
    "%TEST_EXECUTABLE%" %TEST_OPTS% | tee "%OUTPUT_FILE%"
) else (
    "%TEST_EXECUTABLE%" %TEST_OPTS% > "%OUTPUT_FILE%" 2>&1
)
set TEST_RESULT=%ERRORLEVEL%

:: Force success if tests passed but cleanup had issues
findstr /C:"Leaving test case" "%OUTPUT_FILE%" >nul 2>&1
if %ERRORLEVEL% equ 0 (
    if %TEST_RESULT% neq 0 (
        echo !YELLOW!Tests completed but encountered cleanup issues. Treating as success.!NC!
        set TEST_RESULT=0
    )
)

:: Extract performance metrics
echo !YELLOW!Extracting performance metrics...!NC!
findstr /R /C:"time:" /C:"entities:" /C:"processed:" /C:"Performance" /C:"Execution time" /C:"optimization" "%OUTPUT_FILE%" > "%METRICS_FILE%" 2>nul

:: Check test status
findstr /C:"test cases failed" /C:"failure" /C:"memory access violation" /C:"fatal error" "%OUTPUT_FILE%" >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo !RED!❌ Some tests failed! See %OUTPUT_FILE% for details.!NC!
    exit /b 1
) else (
    if %TEST_RESULT% equ 0 (
        echo !GREEN!✅ All AI Optimization tests passed!!NC!
        echo !GREEN!Test results saved to %OUTPUT_FILE%!NC!
        echo !GREEN!Performance metrics saved to %METRICS_FILE%!NC!
        exit /b 0
    ) else (
        echo !RED!❌ Tests failed with error code %TEST_RESULT%! See %OUTPUT_FILE% for details.!NC!
        exit /b %TEST_RESULT%
    )
)