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
  ./run_save_tests.sh --verbose
  ./build/tests/SaveManagerTests --gtest_filter="SaveGameManager*"
  ```
- **Static Analysis:**
  ```
  ./run_all_tests.sh       # includes cppcheck
  cd tests/cppcheck && ./cppcheck_focused.sh
  ```

## Code Style Guidelines

- **C++ Standard:** C++20 preferred for all code.
- **Memory Safety:** Use smart pointers (no raw pointers, no manual new/delete). Follow RAII.
- **Lock-Free Structures:** Use lock-free data structures and atomics where possible for performance and thread safety.
- **Platform-Specific Code:** Respect platform-specific requirements and use preprocessor guards for OS-specific logic.
- **Naming:** Use descriptive, consistent names (CamelCase for types/classes, snake_case for variables/functions).
- **Imports:** Use angle brackets for system/third-party headers, quotes for project headers.
- **Formatting:** 4-space indentation, braces on new lines for classes/functions, keep functions concise.
- **Error Handling:** Always check return values, use try/catch for file/IO, log errors with system-specific macros (e.g., `SAVEGAME_ERROR`, `GAMEENGINE_CRITICAL`).
- **Logging:** Use provided macros for all logging (see `docs/Logger.md`). Only CRITICAL logs in release builds.
- **Testing:** Use Boost.Test macros (`BOOST_AUTO_TEST_CASE`, `BOOST_CHECK`, etc.), clean up resources in fixtures, and always test error cases.
- **Types:** Prefer `std::string`, `std::vector`, and other STL types. Use `const` and references where possible.
- **Thread Safety:** Use atomic operations and proper synchronization in multithreaded code.
- **File/Directory Handling:** Use cross-platform paths, validate permissions, and clean up test artifacts.
- **Documentation:** Comment complex logic, document public APIs, and update docs for new features.

> For more, see `README.md`, `docs/Logger.md`, and `tests/TESTING.md`.

---

Let me know if you want this written to the codebase or need further customization!
