@echo off
REM Script to run the Thread-Safe AI Integration tests
REM Copyright (c) 2025 Hammer Forged Games, MIT License

REM Create required directories
if not exist "test_results" mkdir test_results

REM Set default build type
set BUILD_TYPE=Debug
set CLEAN_BUILD=false
set VERBOSE=false

REM Process command-line options
:parse_args
if "%~1" == "" goto :done_args
if /i "%~1" == "--clean" set CLEAN_BUILD=true
if /i "%~1" == "--release" set BUILD_TYPE=Release
if /i "%~1" == "--verbose" set VERBOSE=true
shift
goto :parse_args
:done_args

REM Configure build cleaning
if "%CLEAN_BUILD%" == "true" (
  echo Cleaning build directory...
  ninja -C build -t clean
)

REM Build the tests
echo Building Thread-Safe AI Integration tests...
if "%VERBOSE%" == "true" (
  ninja -C build thread_safe_ai_integration_tests
) else (
  ninja -C build thread_safe_ai_integration_tests > nul 2>&1
)

REM Check if build was successful
if errorlevel 1 (
  echo Build failed. See output for details.
  exit /b 1
)

REM Determine test executable path based on build type
if "%BUILD_TYPE%" == "Debug" (
  set TEST_EXECUTABLE=.\bin\debug\thread_safe_ai_integration_tests.exe
) else (
  set TEST_EXECUTABLE=.\bin\release\thread_safe_ai_integration_tests.exe
)

REM Verify executable exists
if not exist "%TEST_EXECUTABLE%" (
  echo Error: Test executable not found at '%TEST_EXECUTABLE%'
  echo Searching for test executable...
  for /r "bin" %%f in (thread_safe_ai_integration_tests*.exe) do (
    echo Found executable at: %%f
    set TEST_EXECUTABLE=%%f
    goto :found_executable
  )
  echo Could not find the test executable. Build may have failed or placed the executable in an unexpected location.
  exit /b 1
)

:found_executable

REM Run tests and save output
echo Running Thread-Safe AI Integration tests...

REM Create a temporary file for test output
set TEMP_OUTPUT=test_results\thread_safe_ai_integration_test_output.txt

REM Run the tests
if "%VERBOSE%" == "true" (
  "%TEST_EXECUTABLE%" --log_level=all --catch_system_errors=no --no_result_code > "%TEMP_OUTPUT%" 2>&1
  type "%TEMP_OUTPUT%"
) else (
  "%TEST_EXECUTABLE%" --catch_system_errors=no --no_result_code > "%TEMP_OUTPUT%" 2>&1
  type "%TEMP_OUTPUT%"
)

REM Extract performance metrics
echo Extracting performance metrics...
findstr /C:"time:" /C:"entities:" /C:"processed:" /C:"Concurrent processing time" "%TEMP_OUTPUT%" > "test_results\thread_safe_ai_integration_performance_metrics.txt"

REM Check test status
findstr /C:"test cases failed" "%TEMP_OUTPUT%" > nul
if not errorlevel 1 (
  echo ❌ Some tests failed! See test_results\thread_safe_ai_integration_test_output.txt for details.
  exit /b 1
) else (
  echo ✅ All Thread-Safe AI Integration tests passed!
  exit /b 0
)