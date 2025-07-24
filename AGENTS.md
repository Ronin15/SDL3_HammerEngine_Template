# AGENTS.md

## Build, Lint, and Test Commands

- **Build (Debug):**
  ```
  cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug
  ninja -C build
  ```
- **Build (Release):**
  ```
  cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release
  ninja -C build
  ```
- **Run All Tests:**
  ```
  ./run_all_tests.sh
  ```
- **Run a Single Test:**
  ```
  ./tests/test_scripts/run_save_tests.sh --verbose
  ./bin/debug/SaveManagerTests --run_test="TestSaveAndLoad*"
  ```
- **Static Analysis:**
  ```
  ./tests/test_scripts/run_cppcheck_focused.sh
  ```

**IMPORTANT NOTE:** Test executables are located in `bin/debug/` directory (project root), NOT in `build/bin/debug/`. Test scripts look for binaries in the correct location.

## Code Style Guidelines

- **C++ Standard:** C++20 required, enforced by CMake.
- **Memory Safety:** Use smart pointers (std::shared_ptr, std::unique_ptr). No raw pointers or manual new/delete. Follow RAII principles.
- **Lock-Free Structures:** Use atomic operations and lock-free data structures where possible for performance and thread safety.
- **Platform-Specific Code:** Use preprocessor guards (#ifdef __APPLE__, #ifdef WIN32) for OS-specific logic.
- **Naming:** Use descriptive names - UpperCamelCase for classes/enums/namespaces, lowerCamelCase for functions/variables, m_ prefix for member variables.
- **Imports:** Angle brackets <> for system/third-party headers, quotes "" for project headers. Group system headers first, then project headers.
- **Formatting:** 4-space indentation, braces on new lines for classes/functions, keep functions concise and focused.
- **Error Handling:** Always check return values, use try/catch for file/IO operations. Use logging macros (GAMEENGINE_ERROR, AI_MANAGER_CRITICAL, etc).
- **Logging:** Use provided logging macros extensively (see docs/Logger.md). CRITICAL/ERROR logs only in release builds for performance.
- **Testing:** Use Boost.Test framework with BOOST_AUTO_TEST_CASE, BOOST_CHECK, BOOST_REQUIRE. Clean up test artifacts and test error conditions.
- **Types:** Prefer STL types (std::string, std::vector, std::unordered_map). Use const and references where possible. Use std::string_view for read-only parameters.
- **Threading:** Use ThreadSystem with WorkerBudget for all threading. Avoid raw std::thread - use engine's task scheduling system.
- **Thread Safety:** Use std::atomic, std::shared_mutex, and lock-free structures. Follow cache-friendly data layout (SoA patterns).
- **File/Directory Handling:** Use std::filesystem for cross-platform path handling. Validate file permissions and clean up test artifacts.
- **Documentation:** Comment complex algorithms, document all public APIs with Doxygen-style comments, update relevant docs for new features.
- **Serialization System:** HammerEngine uses ISerializable interface with serialize(std::ostream&) and deserialize(std::istream&) methods. Use BinarySerial::Writer and BinarySerial::Reader classes for binary serialization, NOT BinarySerializer. See SaveGameManager and MockPlayer for examples.
- **Aditional Instructions:** Please ask before removing the build dir, Only remove it when absolutley necessary. Updating cmake and then re-configruing fixes most build problems.
> For more details, see `README.md`, `docs/Logger.md`, `docs/ThreadSystem.md`, and `tests/TESTING.md`.

## Warning Investigation and Fixes

### Unused Variable Warnings
When fixing unused variable warnings, **ALWAYS investigate whether the variable is actually needed** rather than just marking it as unused:

**Investigation Process:**
1. **Build with verbose output to identify all warnings:**
   ```bash
   ninja -C build clean && ninja -C build -v 2>&1 | grep -E "(warning|unused|error)"
   ```

2. **Examine the specific code context:**
   - Read the function where the warning occurs
   - Check if the variable serves a logical purpose
   - Look for similar usage patterns in nearby code
   - Verify if the variable was meant to be used but forgotten

3. **Apply appropriate fix:**
   - **Remove completely** if genuinely unused (preferred)
   - **Fix the logic** if variable should be used but isn't
   - **Only mark as unused** as last resort if needed for API compliance

**Examples of Fixed Issues:**
- `ChaseBehavior.cpp:69` - Removed unused `const AIManager &aiMgr = AIManager::Instance();` that was cached for "performance" but never used since function already uses cached player targets
- `WorldResourceManagerTests.cpp:475` - Removed meaningless `BOOST_CHECK(initialMemoryUsage >= 0);` since `size_t` is unsigned and always >= 0

**Warning Types to Fix:**
- `-Wunused-variable`: Variable declared but never referenced
- `-Wtype-limits`: Comparisons that are always true/false due to type limits
- `-Wunused-parameter`: Function parameters that aren't used

---
