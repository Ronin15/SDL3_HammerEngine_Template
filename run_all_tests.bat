@echo off
:: Backward compatibility wrapper for run_all_tests.bat
:: This script has been moved to tests\test_scripts\run_all_tests.bat

echo NOTE: Test scripts have been moved to tests\test_scripts\
echo Redirecting to tests\test_scripts\run_all_tests.bat...
echo.

:: Get the directory where this script is located
set "SCRIPT_DIR=%~dp0"

:: Execute the actual script in its new location
call "%SCRIPT_DIR%tests\test_scripts\run_all_tests.bat" %*