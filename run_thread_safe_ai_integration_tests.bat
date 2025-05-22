@echo off
REM Script to run the Thread-Safe AI Integration tests
REM Copyright (c) 2025 Hammer Forged Games, MIT License

REM Create required directories
if not exist "build" mkdir build
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
  if exist "build" rmdir /s /q build
  mkdir build
)

REM Create build directory if it doesn't exist
if not exist "build" mkdir build

REM Check if Ninja is available
where ninja >nul 2>&1
if %ERRORLEVEL% EQU 0 (
  set USE_NINJA=true
  echo Ninja build system found, using it for faster builds.
) else (
  set USE_NINJA=false
  echo Ninja build system not found, using default CMake generator.
)

REM Configure the project
echo Configuring project with CMake (Build type: %BUILD_TYPE%)...
if "%USE_NINJA%" == "true" (
  if "%VERBOSE%" == "true" (
    cmake -S . -B build -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -G Ninja
  ) else (
    cmake -S . -B build -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -G Ninja > nul 2>&1
  )
) else (
  if "%VERBOSE%" == "true" (
    cmake -S . -B build -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
  ) else (
    cmake -S . -B build -DCMAKE_BUILD_TYPE=%BUILD_TYPE% > nul 2>&1
  )
)

REM First, update CMakeLists.txt to include the integration test
REM No need to change directory
findstr /C:"thread_safe_ai_integration_tests" tests\CMakeLists.txt >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
  echo Adding integration test to CMakeLists.txt...
  
  REM Create a temporary file with updated content
  set TEMP_FILE=temp_cmake.txt
  
  REM Add executable definition
  type tests\CMakeLists.txt | findstr /v /C:"# Thread-safe AI Integration tests" > %TEMP_FILE%
  
  REM Use PowerShell to do the insertion since batch has limited text processing
  powershell -Command "(Get-Content %TEMP_FILE%) -replace 'add_executable\(thread_safe_ai_manager_tests', '# Thread-safe AI Integration tests\nadd_executable(thread_safe_ai_integration_tests\n    ThreadSafeAIIntegrationTest.cpp\n    ${PROJECT_SOURCE_DIR}/src/managers/AIManager.cpp\n)\n\nadd_executable(thread_safe_ai_manager_tests' | Set-Content %TEMP_FILE%"
  
  REM Add compiler definitions
  powershell -Command "(Get-Content %TEMP_FILE%) -replace 'target_compile_definitions\(thread_safe_ai_manager_tests', '# Thread-safe AI Integration tests definitions\ntarget_compile_definitions(thread_safe_ai_integration_tests PRIVATE\n    BOOST_TEST_NO_LIB\n)\n\ntarget_compile_definitions(thread_safe_ai_manager_tests' | Set-Content %TEMP_FILE%"
  
  REM Add libraries
  powershell -Command "(Get-Content %TEMP_FILE%) -replace 'target_link_libraries\(thread_safe_ai_manager_tests', '# Link Thread-safe AI Integration tests with required libraries\ntarget_link_libraries(thread_safe_ai_integration_tests PRIVATE\n    SDL3::SDL3\n    SDL3_image::SDL3_image\n    Boost::unit_test_framework\n    Boost::container\n)\n\ntarget_link_libraries(thread_safe_ai_manager_tests' | Set-Content %TEMP_FILE%"
  
  REM Add to CTest
  powershell -Command "(Get-Content %TEMP_FILE%) -replace 'add_test\(NAME ThreadSafeAIManagerTests', 'add_test(NAME ThreadSafeAIIntegrationTests COMMAND thread_safe_ai_integration_tests)\nadd_test(NAME ThreadSafeAIManagerTests' | Set-Content %TEMP_FILE%"
  
  REM Replace the original file
  move /y %TEMP_FILE% tests\CMakeLists.txt
)

REM No need to change directory

REM Build the tests
echo Building Thread-Safe AI Integration tests...
if "%USE_NINJA%" == "true" (
  if "%VERBOSE%" == "true" (
    ninja -C build thread_safe_ai_integration_tests
  ) else (
    ninja -C build thread_safe_ai_integration_tests > nul 2>&1
  )
) else (
  if "%VERBOSE%" == "true" (
    cmake --build build --config %BUILD_TYPE% --target thread_safe_ai_integration_tests
  ) else (
    cmake --build build --config %BUILD_TYPE% --target thread_safe_ai_integration_tests > nul 2>&1
  )
)

REM Check if build was successful
if errorlevel 1 (
  echo Build failed. See output for details.
  exit /b 1
)

REM Determine test executable path based on build type and generator
if "%USE_NINJA%" == "true" (
  REM With Ninja, the executable is always in the same location regardless of build type
  set TEST_EXECUTABLE=.\build\tests\thread_safe_ai_integration_tests.exe
) else (
  REM With default CMake generator, executable location depends on build type
  if "%BUILD_TYPE%" == "Debug" (
    set TEST_EXECUTABLE=.\build\tests\Debug\thread_safe_ai_integration_tests.exe
  ) else (
    set TEST_EXECUTABLE=.\build\tests\Release\thread_safe_ai_integration_tests.exe
  )
)

REM Run tests and save output
echo Running Thread-Safe AI Integration tests...

REM Create a temporary file for test output
set TEMP_OUTPUT=%TEMP%\thread_safe_ai_integration_test_output.txt

REM Run the tests
if "%VERBOSE%" == "true" (
  %TEST_EXECUTABLE% --log_level=all > %TEMP_OUTPUT% 2>&1
  type %TEMP_OUTPUT%
) else (
  %TEST_EXECUTABLE% > %TEMP_OUTPUT% 2>&1
  type %TEMP_OUTPUT%
)

REM Save test results
copy %TEMP_OUTPUT% "..\test_results\thread_safe_ai_integration_test_output.txt" > nul

REM Extract performance metrics
echo Extracting performance metrics...
findstr /C:"time:" /C:"entities:" /C:"processed:" /C:"Concurrent processing time" %TEMP_OUTPUT% > "..\test_results\thread_safe_ai_integration_performance_metrics.txt"

REM Clean up temporary file
del %TEMP_OUTPUT%

REM Check test status
findstr /C:"test cases failed" "..\test_results\thread_safe_ai_integration_test_output.txt" > nul
if not errorlevel 1 (
  echo ❌ Some tests failed! See test_results\thread_safe_ai_integration_test_output.txt for details.
  exit /b 1
) else (
  echo ✅ All Thread-Safe AI Integration tests passed!
  exit /b 0
)

REM Already in the root directory