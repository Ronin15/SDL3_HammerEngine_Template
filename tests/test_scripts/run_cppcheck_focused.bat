@echo off
REM Wrapper script for running focused cppcheck analysis from test_scripts directory
REM This script calls the actual cppcheck_focused.bat from tests/cppcheck/

setlocal enabledelayedexpansion

REM Get the directory where this script is located
set "SCRIPT_DIR=%~dp0"

REM Path to the actual cppcheck script
set "CPPCHECK_DIR=%SCRIPT_DIR%..\cppcheck"
set "CPPCHECK_SCRIPT=%CPPCHECK_DIR%\cppcheck_focused.bat"

echo === Running Focused Cppcheck Static Analysis ===
echo.

REM Check if cppcheck script exists
if not exist "%CPPCHECK_SCRIPT%" (
    echo Error: Cppcheck script not found at %CPPCHECK_SCRIPT%
    exit /b 1
)

REM Check if cppcheck is installed
cppcheck --version >nul 2>&1
if errorlevel 1 (
    echo Error: cppcheck not found. Please install cppcheck first.
    echo Download from: https://cppcheck.sourceforge.io/
    exit /b 1
)

REM Change to cppcheck directory and run the analysis
cd /d "%CPPCHECK_DIR%"

REM Run the focused cppcheck analysis
call cppcheck_focused.bat
set RESULT=%ERRORLEVEL%

echo.
if %RESULT% equ 0 (
    echo ✓ Cppcheck static analysis completed successfully
    echo Note: Focus on [error] and [warning] severity issues first
) else (
    echo ✗ Cppcheck static analysis completed with issues detected
    echo Review the output above for critical issues that need fixing
)

echo.
echo For detailed reports and documentation:
echo   Full analysis: cd tests\cppcheck ^&^& run_cppcheck.bat
echo   Documentation: tests\cppcheck\README.md
echo   Fix guide: tests\cppcheck\FIXES.md

exit /b %RESULT%
