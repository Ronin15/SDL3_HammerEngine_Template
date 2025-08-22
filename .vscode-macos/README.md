# macOS VS Code Configuration Guide

This directory contains VS Code configurations optimized for macOS development with Clang.

## Setup Instructions for macOS:

1. **Copy the configuration files:**
   ```bash
   # From your project root
   cp .vscode-macos/* .vscode/
   ```

2. **Install required tools:**
   ```bash
   # Install Xcode Command Line Tools (includes Clang)
   xcode-select --install
   
   # Install Ninja build system
   brew install ninja
   
   # Install LLDB (usually comes with Xcode)
   # LLDB is the default debugger on macOS
   ```

3. **Install recommended VS Code extensions:**
   - C/C++ Extension Pack
   - CMake Tools
   - CodeLLDB (for debugging)
   - clangd (for enhanced IntelliSense)

## Key Differences from Linux:

- **Compiler:** Uses Clang instead of GCC
- **Debugger:** Uses LLDB (native macOS debugger)
- **Library Path:** Uses `DYLD_LIBRARY_PATH` instead of `LD_LIBRARY_PATH`
- **Framework Support:** Includes macOS framework paths
- **Bundle Support:** Handles `.app` bundle creation

## CMake Kits Available:
- **Clang Debug** - Debug builds with full debug info
- **Clang Release** - Optimized release builds
- **Clang AppleSilicon** - Apple Silicon optimized builds

## Debug Configurations:
- **Debug SDL3_Template** - Main executable with auto-build
- **Debug SDL3_Template (No Build)** - Quick debug without rebuilding  
- **Release SDL3_Template** - Release mode debugging
- **Debug Current Test File** - Debug individual test executables

All configurations are pre-configured to work with your existing CMake setup!
