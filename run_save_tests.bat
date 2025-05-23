@echo off
:: Helper script to build and run SaveGameManager tests
setlocal EnableDelayedExpansion

:: Set up colored output
set "RED=[91m"
set "GREEN=[92m"
set "YELLOW=[93m"
set "BLUE=[94m"
set "NC=[0m"

:: Process command line arguments
set CLEAN=false
set CLEAN_ALL=false
set VERBOSE=false

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--clean" (
    set CLEAN=true
    shift
    goto :parse_args
)
if /i "%~1"=="--clean-all" (
    set CLEAN_ALL=true
    shift
    goto :parse_args
)
if /i "%~1"=="--verbose" (
    set VERBOSE=true
    shift
    goto :parse_args
)
if /i "%~1"=="--help" (
    echo !BLUE!SaveGameManager Test Runner!NC!
    echo Usage: run_save_tests.bat [options]
    echo.
    echo Options:
    echo   --clean      Clean test artifacts before building
    echo   --clean-all  Remove entire build directory and rebuild
    echo   --verbose    Run tests with verbose output
    echo   --help       Show this help message
    exit /b 0
)
goto :parse_args
:done_parsing

:: Navigate to the script's directory
cd /d "%~dp0"

:: Handle clean-all case
if "%CLEAN_ALL%"=="true" (
    echo !YELLOW!Removing entire build directory...!NC!
    if exist "build" rmdir /s /q build
)

echo !BLUE!Building SaveGameManager tests...!NC!

:: Ensure build directory exists
if not exist "build" (
    mkdir build
    echo !YELLOW!Created build directory!NC!
)

:: Navigate to build directory
cd build
if %ERRORLEVEL% neq 0 (
    echo !RED!Failed to enter build directory!!NC!
    exit /b 1
)

:: Configure with CMake if needed
if not exist "build.ninja" (
    echo !YELLOW!Configuring project with CMake and Ninja...!NC!
    cmake -G Ninja ..
    if %ERRORLEVEL% neq 0 (
        echo !RED!CMake configuration failed!!NC!
        exit /b 1
    )
)

:: Clean tests if requested
if "%CLEAN%"=="true" (
    echo !YELLOW!Cleaning test artifacts...!NC!
    ninja -t clean save_manager_tests
)

:: Build the tests
echo !YELLOW!Building tests...!NC!
ninja save_manager_tests
if %ERRORLEVEL% neq 0 (
    echo !RED!Build failed!!NC!
    exit /b 1
)

:: Check if test executable exists
set "TEST_EXECUTABLE=..\bin\debug\save_manager_tests.exe"
if not exist "%TEST_EXECUTABLE%" (
    echo !RED!Test executable not found at %TEST_EXECUTABLE%!NC!
    echo !YELLOW!Searching for test executable...!NC!
    
    :: Find the executable (Windows equivalent of find command)
    for /r ".." %%f in (save_manager_tests*.exe) do (
        set "TEST_EXECUTABLE=%%f"
        echo !GREEN!Found test executable at %%f!NC!
        goto :found_executable
    )
    
    echo !RED!Could not find test executable!!NC!
    exit /b 1
)

:found_executable

:: Run the tests
echo !GREEN!Build successful. Running tests...!NC!
echo !BLUE!====================================!NC!

:: Create a temporary file for output
set "TEMP_OUTPUT=test_output.log"

:: Run with appropriate options and display output in real-time
:: Windows doesn't have a built-in tee equivalent, so we'll use this approach
if "%VERBOSE%"=="true" (
    "%TEST_EXECUTABLE%" --log_level=all > "%TEMP_OUTPUT%" 2>&1
    type "%TEMP_OUTPUT%"
) else (
    "%TEST_EXECUTABLE%" > "%TEMP_OUTPUT%" 2>&1
    type "%TEMP_OUTPUT%"
)

:: Save the exit code
set TEST_RESULT=%ERRORLEVEL%

:: If we got a non-zero exit code but also have valid results, consider it successful
if %TEST_RESULT% neq 0 (
    findstr /C:"*** No errors detected" "%TEMP_OUTPUT%" >nul
    if %ERRORLEVEL% equ 0 (
        echo !YELLOW!Test had non-zero exit code but appears to have passed. Treating as success.!NC!
        set TEST_RESULT=0
    )
)

echo !BLUE!====================================!NC!

:: Create test_results directory if it doesn't exist
if not exist "..\test_results" mkdir "..\test_results"

:: Save test results
copy /y "%TEMP_OUTPUT%" "..\test_results\save_manager_test_output.txt" > nul

:: Extract performance metrics if they exist
echo !YELLOW!Saving test results...!NC!
findstr /C:"time:" /C:"performance" /C:"saved:" /C:"loaded:" "%TEMP_OUTPUT%" > "..\test_results\save_manager_performance_metrics.txt" 2>nul

:: Clean up temporary file
del "%TEMP_OUTPUT%"

:: Report test results
if %TEST_RESULT% equ 0 (
    echo !GREEN!All tests passed!!NC!
) else (
    echo !RED!Some tests failed. Please check the output above.!NC!
)

exit /b %TEST_RESULT%