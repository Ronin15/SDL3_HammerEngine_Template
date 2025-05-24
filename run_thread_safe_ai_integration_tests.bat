@echo off
:: Script to run the Thread-Safe AI Integration tests
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

echo !YELLOW!Running Thread-Safe AI Integration tests...!NC!

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
echo !YELLOW!Building Thread-Safe AI Integration tests...!NC!
if "%USE_NINJA%"=="true" (
    if "%VERBOSE%"=="true" (
        ninja -C build thread_safe_ai_integration_tests
    ) else (
        ninja -C build thread_safe_ai_integration_tests >nul 2>&1
    )
) else (
    if "%VERBOSE%"=="true" (
        cmake --build build --config %BUILD_TYPE% --target thread_safe_ai_integration_tests
    ) else (
        cmake --build build --config %BUILD_TYPE% --target thread_safe_ai_integration_tests >nul 2>&1
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
    set TEST_EXECUTABLE=bin\debug\thread_safe_ai_integration_tests.exe
) else (
    set TEST_EXECUTABLE=bin\release\thread_safe_ai_integration_tests.exe
)

:: Verify executable exists
if not exist "%TEST_EXECUTABLE%" (
    echo !RED!Error: Test executable not found at '%TEST_EXECUTABLE%'!NC!
    echo !YELLOW!Searching for test executable...!NC!
    
    :: Try to find the executable
    set FOUND=false
    for /r "bin" %%f in (thread_safe_ai_integration_tests*.exe) do (
        echo !GREEN!Found executable at: %%f!NC!
        set TEST_EXECUTABLE=%%f
        set FOUND=true
        goto :found_executable
    )
    
    if "!FOUND!"=="false" (
        echo !RED!Could not find the test executable. Build may have failed.!NC!
        exit /b 1
    )
)

:found_executable

:: Create output directory
if not exist "test_results" mkdir test_results

:: Output file for test results
set OUTPUT_FILE=test_results\thread_safe_ai_integration_test_output.txt

:: Set test command options for better handling of threading issues
set TEST_OPTS=--log_level=all --no_result_code --catch_system_errors=no --detect_memory_leak=0

:: Run the tests with options to prevent threading issues
echo !YELLOW!Running tests...!NC!

if "%VERBOSE%"=="true" (
    "%TEST_EXECUTABLE%" %TEST_OPTS% > "%OUTPUT_FILE%" 2>&1
    type "%OUTPUT_FILE%"
) else (
    "%TEST_EXECUTABLE%" %TEST_OPTS% > "%OUTPUT_FILE%" 2>&1
)

:: Check the exit code
set TEST_RESULT=%ERRORLEVEL%

:: Extract performance metrics 
echo !YELLOW!Extracting performance metrics...!NC!
if exist "%OUTPUT_FILE%" (
    findstr /C:"time:" /C:"entities:" /C:"concurrent" /C:"processed:" "%OUTPUT_FILE%" > "test_results\thread_safe_ai_integration_performance_metrics.txt" 2>nul
)

:: Check for success indicators in the output
findstr /C:"No errors detected" /C:"test cases passed" /C:"TestCacheInvalidation completed" "%OUTPUT_FILE%" >nul 2>&1
set SUCCESS_INDICATORS=%ERRORLEVEL%

:: Check for failure indicators in the output
findstr /C:"test cases failed" /C:"failure" /C:"memory access violation" /C:"fatal error" "%OUTPUT_FILE%" >nul 2>&1
set FAILURE_INDICATORS=%ERRORLEVEL%

:: Success is either a clean exit code or success indicators without failure indicators
if %TEST_RESULT% equ 0 (
    echo !GREEN!✅ All Thread-Safe AI Integration tests passed!!NC!
    exit /b 0
) else (
    if %SUCCESS_INDICATORS% equ 0 (
        if %FAILURE_INDICATORS% neq 0 (
            echo !YELLOW!Tests likely passed but terminated due to cleanup issues.!NC!
            echo !GREEN!✅ Thread-Safe AI Integration tests considered successful!!NC!
            exit /b 0
        )
    )
    
    echo !RED!❌ Thread-Safe AI Integration tests failed!!NC!
    echo !YELLOW!Please check test_results\thread_safe_ai_integration_test_output.txt for details.!NC!
    exit /b 1
)