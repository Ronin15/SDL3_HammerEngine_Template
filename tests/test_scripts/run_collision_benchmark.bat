@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
call "%SCRIPT_DIR%run_collision_scaling_benchmark.bat" %*
exit /b %errorlevel%
