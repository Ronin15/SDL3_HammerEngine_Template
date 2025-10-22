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
set ERRORS_ONLY=false
set RUN_CORE=true
set RUN_BENCHMARKS=true

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--verbose" (
    set VERBOSE=true
    shift
    goto :parse_args
)
if /i "%~1"=="--errors-only" (
    set ERRORS_ONLY=true
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
    echo   --errors-only     Filter output to show only warnings and errors
    echo   --core-only       Run only core functionality tests ^(fast^)
    echo   --benchmarks-only Run only performance benchmarks ^(slow^)
    echo   --no-benchmarks   Run core tests but skip benchmarks
    echo   --help            Show this help message
    echo.
    echo Test Categories:
    echo   Core Tests:       Thread, AI, Behavior, GameState, Save, Event, Collision, Pathfinding, ParticleManager, Resource Manager, World functionality tests
    echo   Benchmarks:       AI scaling, EventManager scaling, UI stress, ParticleManager, Collision, and Pathfinder performance benchmarks
    echo.
    echo Execution Time:
    echo   Core tests:       ~4-8 minutes total
    echo   Benchmarks:       ~8-20 minutes total
    echo   All tests:        ~12-28 minutes total
    echo.
    echo Examples:
    echo   run_all_tests.bat                 # Run all tests
    echo   run_all_tests.bat --core-only     # Quick validation
    echo   run_all_tests.bat --core-only --errors-only # Fast validation showing only errors
    echo   run_all_tests.bat --no-benchmarks # Skip slow benchmarks
    echo   run_all_tests.bat --benchmarks-only --verbose # Performance testing
    exit /b 0
)
shift
goto :parse_args

:done_parsing

:: Define test categories
:: Core functionality tests (fast execution)
set CORE_TEST_COUNT=21
:: Performance scaling benchmarks (slow execution)
set BENCHMARK_TEST_COUNT=6

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

:: Main execution loop - run core tests first
if "%RUN_CORE%"=="true" (
    echo.
    echo !MAGENTA!Starting core functionality tests...!NC!
    call :run_single_test "run_thread_tests.bat" false
    call :run_single_test "run_buffer_utilization_tests.bat" false
    call :run_single_test "run_thread_safe_ai_tests.bat" false
    call :run_single_test "run_thread_safe_ai_integration_tests.bat" false
    call :run_single_test "run_ai_optimization_tests.bat" false
    call :run_single_test "run_behavior_functionality_tests.bat" false
    call :run_single_test "run_save_tests.bat" false
    call :run_single_test "run_game_state_manager_tests.bat" false
    call :run_single_test "run_event_tests.bat" false
    call :run_single_test "run_weather_event_tests.bat" false
    call :run_single_test "run_particle_manager_tests.bat" false
    call :run_single_test "run_json_reader_tests.bat" false
    call :run_single_test "run_resource_tests.bat" false
    call :run_single_test "run_resource_edge_case_tests.bat" false
    call :run_single_test "run_world_generator_tests.bat" false
    call :run_single_test "run_world_manager_event_integration_tests.bat" false
    call :run_single_test "run_world_manager_tests.bat" false
    call :run_single_test "run_world_resource_manager_tests.bat" false
    call :run_single_test "run_collision_tests.bat" false
    call :run_single_test "run_pathfinding_tests.bat" false
    call :run_single_test "run_collision_pathfinding_integration_tests.bat" false
)

:: Run benchmark tests last
if "%RUN_BENCHMARKS%"=="true" (
    echo.
    echo !MAGENTA!Starting performance benchmarks...!NC!
    call :run_single_test "run_event_scaling_benchmark.bat" true
    call :run_single_test "run_ai_benchmark.bat" true
    call :run_single_test "run_ui_stress_tests.bat" true
    call :run_single_test "run_particle_manager_benchmark.bat" true
    call :run_single_test "run_collision_benchmark.bat" true
    call :run_single_test "run_pathfinder_benchmark.bat" true
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

:: Function to run a single test script
:run_single_test
set "test_script=%~1"
set "is_benchmark=%~2"

:: Show test header
if "%is_benchmark%"=="true" (
    echo.
    echo Running benchmark: %test_script%
) else (
    echo.
    echo Running test: %test_script%
)

:: Check if script exists
if not exist "%test_script%" (
    echo ERROR: Script not found: %test_script%
    echo FAILED: Script not found: %test_script%>> "!COMBINED_RESULTS!"
    set OVERALL_SUCCESS=false
    set /a FAILED_COUNT+=1
    goto :eof
)

:: Prepare arguments
set test_args=
if "%VERBOSE%"=="true" set test_args=%test_args% --verbose

:: Execute the test
if "%ERRORS_ONLY%"=="true" (
    :: Errors-only mode: capture output and filter
    set "temp_file=%TEMP%\test_output_%RANDOM%.txt"
    call "%test_script%" %test_args% >"!temp_file!" 2>&1
    set test_exit_code=!ERRORLEVEL!
    
    :: Check for failures - use more specific patterns for actual test failures
    set has_failures=0
    findstr /I /C:"BOOST_CHECK.*failed" /C:"BOOST_REQUIRE.*failed" /C:"Test.*failed" /C:"FAILED.*test" /C:"BUILD FAILED" /C:"compilation.*failed" /C:"Segmentation fault" /C:"Assertion.*failed" "!temp_file!" >nul 2>&1
    if !ERRORLEVEL! equ 0 set has_failures=1
    
    :: Show failures if found
    if !has_failures! equ 1 (
        echo FAILURES DETECTED:
        findstr /I /C:"BOOST_CHECK.*failed" /C:"BOOST_REQUIRE.*failed" /C:"Test.*failed" /C:"FAILED.*test" /C:"BUILD FAILED" /C:"compilation.*failed" /C:"Segmentation fault" /C:"Assertion.*failed" "!temp_file!"
    ) else (
        if not "!test_exit_code!"=="0" (
            echo Test failed with exit code !test_exit_code!
        )
    )
    
    :: Clean up temp file
    if exist "!temp_file!" del "!temp_file!" >nul 2>&1) else (
    :: Normal mode: show all output
    call "%test_script%" %test_args%
    set test_exit_code=!ERRORLEVEL!
)

:: Record results
if "!test_exit_code!"=="0" (
    echo PASSED: %test_script%
    echo PASSED: %test_script%>> "!COMBINED_RESULTS!"
    set /a PASSED_COUNT+=1
) else (
    echo FAILED: %test_script% ^(exit code: !test_exit_code!^)
    echo FAILED: %test_script% ^(exit code: !test_exit_code!^)>> "!COMBINED_RESULTS!"
    set OVERALL_SUCCESS=false
    set /a FAILED_COUNT+=1
)

:: Brief pause between tests
timeout /t 1 /nobreak >nul 2>&1

goto :eof