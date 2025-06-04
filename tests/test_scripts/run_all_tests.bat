@echo off
:: Script to run all test shell scripts sequentially

:: Enable color output on Windows 10+ terminals
setlocal EnableDelayedExpansion

:: Set up colored output
set "RED=[91m"
set "GREEN=[92m"
set "YELLOW=[93m"
set "BLUE=[94m"
set "MAGENTA=[95m"
set "CYAN=[96m"
set "NC=[0m"

:: Directory where all scripts are located
set "SCRIPT_DIR=%~dp0"

:: Navigate to script directory
cd /d "%SCRIPT_DIR%"

:: Process command line arguments
set VERBOSE=false
set RUN_CORE=true
set RUN_BENCHMARKS=true

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--verbose" (
    set VERBOSE=true
    shift
    goto :parse_args
)
if /i "%~1"=="--core-only" (
    set RUN_CORE=true
    set RUN_BENCHMARKS=false
    shift
    goto :parse_args
)
if /i "%~1"=="--benchmarks-only" (
    set RUN_CORE=false
    set RUN_BENCHMARKS=true
    shift
    goto :parse_args
)
if /i "%~1"=="--no-benchmarks" (
    set RUN_CORE=true
    set RUN_BENCHMARKS=false
    shift
    goto :parse_args
)
if /i "%~1"=="--help" (
    echo !BLUE!All Tests Runner!NC!
    echo Usage: run_all_tests.bat [options]
    echo.
    echo Options:
    echo   --verbose         Run tests with verbose output
    echo   --core-only       Run only core functionality tests ^(fast^)
    echo   --benchmarks-only Run only performance benchmarks ^(slow^)
    echo   --no-benchmarks   Run core tests but skip benchmarks
    echo   --help            Show this help message
    echo.
    echo Test Categories:
    echo   Core Tests:       Thread, AI, Save, Event functionality tests
    echo   Benchmarks:       AI scaling, EventManager scaling, and UI stress benchmarks
    echo.
    echo Execution Time:
    echo   Core tests:       ~2-5 minutes total
    echo   Benchmarks:       ~5-15 minutes total
    echo   All tests:        ~7-20 minutes total
    echo.
    echo Examples:
    echo   run_all_tests.bat                 # Run all tests
    echo   run_all_tests.bat --core-only     # Quick validation
    echo   run_all_tests.bat --no-benchmarks # Skip slow benchmarks
    echo   run_all_tests.bat --benchmarks-only --verbose # Performance testing
    exit /b 0
)
shift
goto :parse_args

:done_parsing

:: Define test categories
:: Core functionality tests (fast execution)
set CORE_TEST_COUNT=6
set CORE_TEST_1=run_thread_tests.bat
set CORE_TEST_2=run_thread_safe_ai_tests.bat
set CORE_TEST_3=run_thread_safe_ai_integration_tests.bat
set CORE_TEST_4=run_ai_optimization_tests.bat
set CORE_TEST_5=run_save_tests.bat
set CORE_TEST_6=run_event_tests.bat

:: Performance scaling benchmarks (slow execution)
set BENCHMARK_TEST_COUNT=3
set BENCHMARK_TEST_1=run_event_scaling_benchmark.bat
set BENCHMARK_TEST_2=run_ai_benchmark.bat
set BENCHMARK_TEST_3=run_ui_stress_tests.bat

:: Build the test scripts array based on user selection
set TOTAL_COUNT=0
if "%RUN_CORE%"=="true" (
    set /a TOTAL_COUNT+=!CORE_TEST_COUNT!
)
if "%RUN_BENCHMARKS%"=="true" (
    set /a TOTAL_COUNT+=!BENCHMARK_TEST_COUNT!
)

:: Create a directory for the combined test results
if not exist "..\..\test_results\combined" mkdir "..\..\test_results\combined"
set "COMBINED_RESULTS=..\..\test_results\combined\all_tests_results.txt"
echo All Tests Run %date% %time%> "!COMBINED_RESULTS!"

:: Track overall success
set OVERALL_SUCCESS=true
set PASSED_COUNT=0
set FAILED_COUNT=0

:: Print header with execution plan
echo !BLUE!======================================================!NC!
echo !BLUE!              Running Test Scripts                    !NC!
echo !BLUE!======================================================!NC!

:: Show execution plan
if "%RUN_CORE%"=="true" (
    if "%RUN_BENCHMARKS%"=="true" (
        echo !YELLOW!Execution Plan: All tests ^(!CORE_TEST_COUNT! core + !BENCHMARK_TEST_COUNT! benchmarks^)!NC!
        echo !YELLOW!Note: Performance benchmarks will run last and may take several minutes!NC!
    ) else (
        echo !YELLOW!Execution Plan: Core functionality tests only ^(!CORE_TEST_COUNT! tests^)!NC!
        echo !GREEN!Fast execution mode - skipping performance benchmarks!NC!
    )
) else (
    if "%RUN_BENCHMARKS%"=="true" (
        echo !YELLOW!Execution Plan: Performance benchmarks only ^(!BENCHMARK_TEST_COUNT! benchmarks^)!NC!
        echo !YELLOW!Note: This will take several minutes to complete!NC!
    ) else (
        echo !RED!Error: No test categories selected!NC!
        exit /b 1
    )
)

echo !CYAN!Found !TOTAL_COUNT! test scripts to run!NC!

:: Run core tests first
if "%RUN_CORE%"=="true" (
    for /L %%i in (1,1,!CORE_TEST_COUNT!) do (
        call :run_test_script !CORE_TEST_%%i! false
        
        :: Add a small delay between tests to ensure resources are released
        timeout /t 2 /nobreak >nul 2>&1
    )
)

:: Run benchmark tests last
if "%RUN_BENCHMARKS%"=="true" (
    for /L %%i in (1,1,!BENCHMARK_TEST_COUNT!) do (
        call :run_test_script !BENCHMARK_TEST_%%i! true
        
        :: Add delay for benchmarks to ensure proper resource cleanup
        echo !YELLOW!Allowing time for resource cleanup after benchmark...!NC!
        timeout /t 2 /nobreak >nul 2>&1
    )
)

:: Print summary
echo.
echo !BLUE!======================================================!NC!
echo !BLUE!                  Test Summary                       !NC!
echo !BLUE!======================================================!NC!
echo Total scripts: !TOTAL_COUNT!
echo !GREEN!Passed: !PASSED_COUNT!!NC!
echo !RED!Failed: !FAILED_COUNT!!NC!

:: Save summary to results file
echo.>> "!COMBINED_RESULTS!"
echo Summary:>> "!COMBINED_RESULTS!"
echo Total: !TOTAL_COUNT!>> "!COMBINED_RESULTS!"
echo Passed: !PASSED_COUNT!>> "!COMBINED_RESULTS!"
echo Failed: !FAILED_COUNT!>> "!COMBINED_RESULTS!"
echo Completed at: %date% %time%>> "!COMBINED_RESULTS!"

:: Exit with appropriate status code and summary
if "%OVERALL_SUCCESS%"=="true" (
    if "%RUN_CORE%"=="true" (
        if "%RUN_BENCHMARKS%"=="true" (
            echo.
            echo !GREEN!All test scripts completed successfully!!NC!
        ) else (
            echo.
            echo !GREEN!All core functionality tests completed successfully!!NC!
            echo !CYAN!To run performance benchmarks: run_all_tests.bat --benchmarks-only!NC!
        )
    ) else (
        if "%RUN_BENCHMARKS%"=="true" (
            echo.
            echo !GREEN!All performance benchmarks completed successfully!!NC!
        )
    )
    exit /b 0
) else (
    if "%RUN_CORE%"=="true" (
        if "%RUN_BENCHMARKS%"=="true" (
            echo.
            echo !RED!Some test scripts failed. Please check the individual test results.!NC!
        ) else (
            echo.
            echo !RED!Some core functionality tests failed. Please check the individual test results.!NC!
        )
    ) else (
        if "%RUN_BENCHMARKS%"=="true" (
            echo.
            echo !RED!Some performance benchmarks failed. Please check the individual test results.!NC!
        )
    )
    echo Combined results saved to: !YELLOW!!COMBINED_RESULTS!!NC!
    exit /b 1
)

:: Function to run a test script
:run_test_script
setlocal EnableDelayedExpansion
set script=%~1
set is_benchmark=%~2
set args=

:: Pass along relevant flags
if "%VERBOSE%"=="true" (
    set args=!args! --verbose
)

:: Special handling for scaling benchmarks
if "%is_benchmark%"=="true" (
    echo.
    echo !MAGENTA!=====================================================!NC!
    echo !CYAN!Running performance benchmark: !YELLOW!%script%!NC!
    echo !MAGENTA!This may take several minutes...!NC!
    echo !MAGENTA!=====================================================!NC!
) else (
    echo.
    echo !MAGENTA!=====================================================!NC!
    echo !CYAN!Running test script: !YELLOW!%script%!NC!
    echo !MAGENTA!=====================================================!NC!
)

:: Check if the script exists
if not exist "%script%" (
    echo !RED!Script not found: %script%!NC!
    echo FAILED: Script not found: %script%>> "!COMBINED_RESULTS!"
    endlocal & set OVERALL_SUCCESS=false & set /a FAILED_COUNT+=1
    exit /b 1
)

:: Run the script with provided arguments
call "%script%" !args!
set result=!ERRORLEVEL!

if !result! equ 0 (
    if "%is_benchmark%"=="true" (
        echo.
        echo !GREEN!✓ Performance benchmark %script% completed successfully!NC!
    ) else (
        echo.
        echo !GREEN!✓ Test script %script% completed successfully!NC!
    )
    echo PASSED: %script%>> "!COMBINED_RESULTS!"
    endlocal & set /a PASSED_COUNT+=1
    exit /b 0
) else (
    if "%is_benchmark%"=="true" (
        echo.
        echo !RED!✗ Performance benchmark %script% failed with exit code !result!!NC!
    ) else (
        echo.
        echo !RED!✗ Test script %script% failed with exit code !result!!NC!
    )
    echo FAILED: %script% ^(exit code: !result!^)>> "!COMBINED_RESULTS!"
    endlocal & set OVERALL_SUCCESS=false & set /a FAILED_COUNT+=1
    exit /b 1
)