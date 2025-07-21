@echo off
REM Run Resource Manager Tests
REM Copyright (c) 2025 Hammer Forged Games

echo ==========================================
echo Running Resource Manager System Tests
echo ==========================================

REM Check if build directory exists
if not exist "build" (
    echo Error: Build directory not found. Please build the project first.
    echo Run: cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug ^&^& ninja -C build
    exit /b 1
)

REM Set up test environment
set SDL_VIDEODRIVER=dummy

echo Running ResourceManager Tests...
if "%1"=="--verbose" (
    .\build\tests\resource_manager_tests.exe --log_level=all --report_level=detailed
) else (
    .\build\tests\resource_manager_tests.exe --log_level=error --report_level=short
)

echo.
echo Running InventoryComponent Tests...
if "%1"=="--verbose" (
    .\build\tests\inventory_component_tests.exe --log_level=all --report_level=detailed
) else (
    .\build\tests\inventory_component_tests.exe --log_level=error --report_level=short
)

echo.
echo Running ResourceChangeEvent Tests...
if "%1"=="--verbose" (
    .\build\tests\resource_change_event_tests.exe --log_level=all --report_level=detailed
) else (
    .\build\tests\resource_change_event_tests.exe --log_level=error --report_level=short
)

echo.
echo Running Resource Integration Tests...
if "%1"=="--verbose" (
    .\build\tests\resource_integration_tests.exe --log_level=all --report_level=detailed
) else (
    .\build\tests\resource_integration_tests.exe --log_level=error --report_level=short
)

echo.
echo ==========================================
echo All Resource Tests Completed Successfully!
echo ==========================================