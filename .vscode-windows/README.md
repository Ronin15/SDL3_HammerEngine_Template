# Windows VS Code Configuration Guide

This directory contains VS Code configurations optimized for Windows development with MSVC/MinGW.

## Setup Instructions for Windows:

1. **Copy the configuration files:**
   ```cmd
   # From your project root in PowerShell or Command Prompt
   xcopy .vscode-windows\* .vscode\ /E /Y
   ```
   
   Or in PowerShell:
   ```powershell
   Copy-Item -Path ".vscode-windows\*" -Destination ".vscode\" -Recurse -Force
   ```

2. **Install required tools:**
   ```cmd
   # Install Visual Studio 2022 with C++ workload (includes MSVC)
   # OR install MinGW-w64 + Ninja
   
   # Install Ninja build system (if using MinGW)
   choco install ninja
   # OR download from: https://github.com/ninja-build/ninja/releases
   
   # Install Git for Windows (includes bash)
   # Install CMake for Windows
   ```

3. **Install recommended VS Code extensions:**
   - C/C++ Extension Pack
   - CMake Tools
   - C++ TestMate (for test discovery)
   - CodeLLDB or C/C++ Extension (for debugging)

## Key Differences from Linux:

- **Compiler:** Uses MSVC or MinGW instead of GCC
- **Debugger:** Uses C/C++ Extension debugger or CodeLLDB
- **Library Path:** Uses `PATH` instead of `LD_LIBRARY_PATH`
- **Executable Extension:** `.exe` files
- **Path Separators:** Uses backslashes `\` in Windows paths

## CMake Kits Available:
- **MSVC Debug** - Visual Studio compiler debug builds
- **MSVC Release** - Visual Studio compiler optimized builds
- **MinGW Debug** - MinGW-w64 debug builds
- **MinGW Release** - MinGW-w64 release builds

## Debug Configurations:
- **Debug SDL3_Template** - Main executable with auto-build
- **Debug SDL3_Template (No Build)** - Quick debug without rebuilding  
- **Release SDL3_Template** - Release mode debugging
- **Debug Current Test File** - Debug individual test executables

All configurations are pre-configured to work with your existing CMake setup!
