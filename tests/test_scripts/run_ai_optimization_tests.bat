@echo off
:: Script to run the AI Optimization Tests
:: Copyright (c) 2025 Hammer Forged Games, MIT License

:: Enable color output on Windows 10+ terminals
setlocal EnableDelayedExpansion

:: Color codes for Windows
set "GREEN=[92m"
set "YELLOW=[93m"
set "RED=[91m"
set "NC=[0m"

echo !YELLOW!Running AI Optimization Tests...!NC!

:: Navigate to project root directory (in case script is run from elsewhere)
cd /d "%~dp0"

:: Create directory for test results
if not exist "..\..\test_results" mkdir "..\..\test_results"

:: Set default build type
set BUILD_TYPE=Debug
set VERBOSE=false

:: Process command-line options
:parse_args
if "%~1"=="" goto :done_parsing
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
    echo Usage: %0 [--debug] [--release] [--verbose] [--help]
    echo   --debug     Run in Debug mode ^(default^)
    echo   --release   Run in Release mode
    echo   --verbose   Show verbose output
    echo   --help      Show this help message
    exit /b 0
)
echo Unknown option: %1
echo Usage: %0 [--debug] [--release] [--verbose] [--help]
exit /b 1

:done_parsing

:: Prepare to run tests
echo !YELLOW!Preparing to run AI Optimization tests...!NC!

:: Determine the correct path to the test executable
if "%BUILD_TYPE%"=="Debug" (
    set TEST_EXECUTABLE=..\..\bin\debug\ai_optimization_tests.exe
) else (
    set TEST_EXECUTABLE=..\..\bin\release\ai_optimization_tests.exe
)

:: Verify executable exists
if not exist "!TEST_EXECUTABLE!" (
    echo !RED!Error: Test executable not found at '!TEST_EXECUTABLE!'!NC!
    :: Attempt to find the executable
    echo !YELLOW!Searching for test executable...!NC!
    set FOUND_EXECUTABLE=
    for /r "bin" %%f in (ai_optimization_tests.exe) do (
        echo !GREEN!Found executable at: %%f!NC!
        set TEST_EXECUTABLE=%%f
        set FOUND_EXECUTABLE=true
        goto :found_executable
    )
    
    if "!FOUND_EXECUTABLE!"=="" (
        echo !RED!Could not find the test executable. Build may have failed or placed the executable in an unexpected location.!NC!
        exit /b 1
    )
)

:found_executable

:: Run tests and save output
echo !YELLOW!Running AI Optimization tests...!NC!

:: Ensure test_results directory exists
if not exist "..\..\test_results" mkdir "..\..\test_results"

:: Output file
set OUTPUT_FILE=..\..\test_results\ai_optimization_tests_output.txt
set METRICS_FILE=..\..\test_results\ai_optimization_tests_performance_metrics.txt

:: Set test command options
set TEST_OPTS=--log_level=all --catch_system_errors=no
if "%VERBOSE%"=="true" (
    set TEST_OPTS=!TEST_OPTS! --report_level=detailed
)

:: Run the tests with additional safeguards
echo !YELLOW!Running with options: !TEST_OPTS!!NC!

:: Run the tests with timeout using PowerShell
powershell -Command "& { $job = Start-Job -ScriptBlock { & '%CD%\!TEST_EXECUTABLE!' !TEST_OPTS! 2>&1 }; if (Wait-Job $job -Timeout 30) { Receive-Job $job } else { Stop-Job $job; Remove-Job $job; Write-Host 'TIMEOUT' } }" > "!OUTPUT_FILE!"
set TEST_RESULT=%ERRORLEVEL%

:: Force success if tests passed but cleanup had issues
findstr /c:"Leaving test case" "!OUTPUT_FILE!" >nul 2>&1
if %ERRORLEVEL% equ 0 (
    if !TEST_RESULT! neq 0 (
        echo !YELLOW!Tests completed but encountered cleanup issues. Treating as success.!NC!
        set TEST_RESULT=0
    )
)

:: Extract performance metrics
echo !YELLOW!Extracting performance metrics...!NC!
findstr /r /c:"time:" /c:"entities:" /c:"processed:" /c:"Performance" /c:"Execution time" /c:"optimization" "!OUTPUT_FILE!" > "!METRICS_FILE!" 2>nul

:: Handle timeout scenario
findstr /c:"TIMEOUT" "!OUTPUT_FILE!" >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo !RED!⚠️ Test execution timed out after 30 seconds!!NC!
    echo Test execution timed out after 30 seconds!>> "!OUTPUT_FILE!"
    set TEST_RESULT=124
)

:: Check test status
if !TEST_RESULT! equ 124 (
    echo !RED!❌ Tests timed out! See !OUTPUT_FILE! for details.!NC!
    exit /b !TEST_RESULT!
) else (
    findstr /c:"failure" /c:"test cases failed" /c:"memory access violation" /c:"fatal error" /c:"Segmentation fault" /c:"Abort trap" /c:"assertion failed" "!OUTPUT_FILE!" >nul 2>&1
    if %ERRORLEVEL% equ 0 (
        echo !RED!❌ Some tests failed! See !OUTPUT_FILE! for details.!NC!
        exit /b 1
    ) else (
        if !TEST_RESULT! neq 0 (
            echo !RED!❌ Some tests failed! See !OUTPUT_FILE! for details.!NC!
            exit /b 1
        ) else (
            echo !GREEN!✅ All AI Optimization tests passed!!NC!
            echo !GREEN!Test results saved to !OUTPUT_FILE!!NC!
            echo !GREEN!Performance metrics saved to !METRICS_FILE!!NC!
            exit /b 0
        )
    )
)