@echo off
:: Script to run the ThreadSystem Tests
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
if /i "%~1"=="--verbose" (
    set VERBOSE=true
    shift
    goto :parse_args
)
if /i "%~1"=="--release" (
    set BUILD_TYPE=Release
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

echo !BLUE!Running ThreadSystem tests...!NC!

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
echo !YELLOW!Building ThreadSystem tests...!NC!
if "%USE_NINJA%"=="true" (
    if "%VERBOSE%"=="true" (
        ninja -C build thread_system_tests
    ) else (
        ninja -C build thread_system_tests >nul 2>&1
    )
) else (
    if "%VERBOSE%"=="true" (
        cmake --build build --config %BUILD_TYPE% --target thread_system_tests
    ) else (
        cmake --build build --config %BUILD_TYPE% --target thread_system_tests >nul 2>&1
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
    set TEST_EXECUTABLE=bin\debug\thread_system_tests.exe
) else (
    set TEST_EXECUTABLE=bin\release\thread_system_tests.exe
)

:: Verify executable exists
if not exist "%TEST_EXECUTABLE%" (
    echo !RED!Error: Test executable not found at '%TEST_EXECUTABLE%'!NC!
    echo !YELLOW!Searching for test executable...!NC!
    for /r "bin" %%f in (thread_system_tests*.exe) do (
        echo !GREEN!Found executable at: %%f!NC!
        set TEST_EXECUTABLE=%%f
        goto :found_executable
    )
    echo !RED!Could not find the test executable. Build may have failed.!NC!
    exit /b 1
)

:found_executable

:: Create test_results directory if it doesn't exist
if not exist "test_results" mkdir test_results

:: Run tests and save output
echo !GREEN!Running tests...!NC!
echo !BLUE!====================================!NC!

:: Set test command options
set TEST_OPTS=--catch_system_errors=no --no_result_code
if "%VERBOSE%"=="true" (
    set TEST_OPTS=%TEST_OPTS% --log_level=all
)

:: Run the tests and save output to a temporary file
echo !YELLOW!Running with options: %TEST_OPTS%!NC!
set TEMP_OUTPUT=test_output.log

if "%VERBOSE%"=="true" (
    "%TEST_EXECUTABLE%" %TEST_OPTS% > "%TEMP_OUTPUT%" 2>&1
    type "%TEMP_OUTPUT%"
) else (
    "%TEST_EXECUTABLE%" %TEST_OPTS% > "%TEMP_OUTPUT%" 2>&1
)
set TEST_RESULT=%ERRORLEVEL%

echo !BLUE!====================================!NC!

:: Save test results
copy "%TEMP_OUTPUT%" "test_results\thread_system_test_output.txt" >nul

:: Extract performance metrics
echo !YELLOW!Saving test results...!NC!
findstr /R /C:"time:" /C:"performance" /C:"tasks:" /C:"queue:" "%TEMP_OUTPUT%" > "test_results\thread_system_performance_metrics.txt" 2>nul

:: Clean up temporary file
del "%TEMP_OUTPUT%"

:: Report test results
if %TEST_RESULT% equ 0 (
    echo !GREEN!All tests passed!!NC!
) else (
    echo !RED!Some tests failed. Please check test_results\thread_system_test_output.txt for details.!NC!
)

exit /b %TEST_RESULT%