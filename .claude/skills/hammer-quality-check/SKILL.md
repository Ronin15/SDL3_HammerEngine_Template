---
name: hammer-quality-check
description: Runs comprehensive code quality checks for SDL3 HammerEngine including compilation warnings, static analysis (cppcheck), coding standards validation, threading safety verification, and architecture compliance. Use before commits, pull requests, or when the user wants to verify code meets project quality standards.
allowed-tools: [Bash, Read, Grep]
---

# HammerEngine Code Quality Gate

This Skill enforces SDL3 HammerEngine's quality standards as defined in `CLAUDE.md`. It performs comprehensive checks to catch issues before they reach version control.

## Quality Gate Categories

1. **Compilation Quality** - Zero warnings policy
2. **Static Analysis** - Memory safety, null pointers, threading
3. **Coding Standards** - Naming conventions, formatting
4. **Threading Safety** - Critical threading rules enforcement
5. **Architecture Compliance** - Design pattern adherence
6. **Copyright & Legal** - License header validation
7. **Test Coverage** - Verify tests exist for modified code

## Detailed Checks

### 1. Compilation Quality

**Command:**
```bash
ninja -C build -v 2>&1 | grep -E "(warning|unused|error)" | head -n 100
```

**Working Directory:** `/home/RoninXV/projects/cpp_projects/SDL3_HammerEngine_Template/`

**Checks:**
- Count total warnings
- Categorize warning types:
  - Unused variables/parameters
  - Uninitialized members
  - Type conversion warnings
  - Shadowing warnings
  - Deprecated usage
  - Sign comparison warnings

**Quality Gate:** ✓ Zero compilation warnings required

**Common Issues:**
```cpp
// ✗ BAD
int x;  // uninitialized
void func(int unused) { }  // unused parameter

// ✓ GOOD
int x = 0;
void func([[maybe_unused]] int param) { }
```

### 2. Static Analysis (cppcheck)

**Command:**
```bash
./tests/cppcheck/cppcheck_focused.sh
```

**Or if script not available:**
```bash
cppcheck --enable=all --suppress=missingIncludeSystem \
  --std=c++20 --quiet \
  src/ include/ 2>&1
```

**Checks:**
- Memory leaks
- Null pointer dereferences
- Buffer overflows
- Use after free
- Double free
- Uninitialized variables
- Dead code / unreachable code
- Thread safety issues

**Quality Gate:** ✓ Zero critical/error severity issues

**Severity Levels:**
- **error:** Must fix (blocks commit)
- **warning:** Should fix (review required)
- **style:** Optional (improve if time permits)
- **performance:** Consider optimizing
- **information:** FYI only

### 3. Coding Standards (CLAUDE.md Compliance)

#### 3.1 Naming Conventions

**Check Commands:**
```bash
# Find potential naming violations
grep -rn "class [a-z]" src/ include/  # Classes must be UpperCamelCase
grep -rn "^[A-Z][a-z]*(" src/*.cpp    # Functions should be lowerCamelCase
```

**Standards:**

| Item | Convention | Example |
|------|-----------|---------|
| Classes/Enums | UpperCamelCase | `GameEngine`, `EntityType` |
| Functions/Variables | lowerCamelCase | `updateEntity()`, `deltaTime` |
| Member Variables | `m_` prefix | `m_entityCount` |
| Member Pointers | `mp_` prefix | `mp_renderer` |
| Constants | ALL_CAPS | `MAX_ENTITIES` |
| Namespaces | lowercase | `namespace utils` |

**Automated Checks:**
```bash
# Check for member variables without m_ prefix (in .cpp files)
grep -rn "^\s*[a-z][a-zA-Z0-9]*\s*;" src/ include/ | grep -v "m_" | grep -v "mp_"

# Check for class names starting with lowercase
grep -rn "^class [a-z]" include/
```

**Quality Gate:** ✓ All naming conventions followed

#### 3.2 Formatting Standards

**Standards:**
- **Indentation:** 4 spaces (no tabs)
- **Braces:** Allman style (braces on new line)
- **Line length:** Reasonable (no hard limit, but keep readable)

**Example:**
```cpp
// ✓ GOOD - Allman braces, 4-space indent
void GameEngine::update(float deltaTime)
{
    if (m_isRunning)
    {
        processEvents();
        updateSystems(deltaTime);
    }
}

// ✗ BAD - K&R braces, wrong indent
void GameEngine::update(float deltaTime) {
  if (m_isRunning) {
    processEvents();
  }
}
```

### 4. Threading Safety (CRITICAL)

**FORBIDDEN PATTERNS:**

#### 4.1 Static Variables in Threaded Code

**Check Command:**
```bash
# Find static variables in .cpp files (potential threading hazard)
grep -rn "static [^v].*=" src/ --include="*.cpp" | grep -v "static_cast" | grep -v "static const"
```

**Rule from CLAUDE.md:**
> **NEVER static vars in threaded code** (use instance vars, thread_local, or atomics)

**Why This is Critical:**
- HammerEngine uses separate update/render threads
- Static variables cause data races
- Non-deterministic behavior and crashes

**Example Violations:**
```cpp
// ✗ FORBIDDEN - static variable in threaded code
void AIManager::updateBehaviors()
{
    static int frameCount = 0;  // RACE CONDITION!
    frameCount++;
}

// ✓ GOOD - instance variable
class AIManager
{
    int m_frameCount = 0;  // Thread-safe with proper locking
};

// ✓ GOOD - thread_local if needed per-thread
void AIManager::updateBehaviors()
{
    thread_local int threadFrameCount = 0;
    threadFrameCount++;
}
```

**Quality Gate:** ✓ Zero static variables in threaded code (BLOCKING)

#### 4.2 Raw std::thread Usage

**Check Command:**
```bash
grep -rn "std::thread" src/ include/ | grep -v "ThreadSystem"
```

**Rule from CLAUDE.md:**
> Use ThreadSystem (not raw std::thread)

**Why:**
- ThreadSystem provides WorkerBudget priorities
- Prevents thread explosion
- Better resource management

**Quality Gate:** ✓ No raw std::thread usage

#### 4.3 Mutex Protection

**Check Command:**
```bash
# Find managers that should have mutex protection
grep -rn "class.*Manager" include/managers/
```

**For each manager, verify:**
- Has `std::mutex m_mutex;` member
- Update functions use `std::lock_guard<std::mutex> lock(m_mutex);`
- Render access uses proper locking

**Quality Gate:** ✓ All managers have proper mutex protection

### 5. Architecture Compliance

#### 5.1 No Background Thread Rendering

**Check Command:**
```bash
# Find potential rendering calls outside main render function
grep -rn "SDL_Render" src/ | grep -v "GameEngine::render" | grep -v "//.*SDL_Render"
```

**Rule from CLAUDE.md:**
> Render (main thread only, double-buffered)

**Quality Gate:** ✓ No rendering outside GameEngine::render()

#### 5.2 RAII & Smart Pointers

**Check Command:**
```bash
# Find raw new/delete usage (prefer smart pointers)
grep -rn "new " src/ include/ | grep -v "std::make_" | grep -v "//"
grep -rn "delete " src/ include/ | grep -v "//"
```

**Rule from CLAUDE.md:**
> RAII + smart pointers

**Prefer:**
- `std::unique_ptr` for exclusive ownership
- `std::shared_ptr` for shared ownership
- `std::make_unique` / `std::make_shared` for creation

**Quality Gate:** ✓ Minimal raw new/delete (exceptions allowed for SDL resources)

#### 5.3 Logger Usage

**Check Command:**
```bash
# Find std::cout usage (should use Logger instead)
grep -rn "std::cout" src/ | grep -v "//"
grep -rn "std::cerr" src/ | grep -v "//"
grep -rn "printf" src/ | grep -v "//"
```

**Rule from CLAUDE.md:**
> Logger macros

**Correct Usage:**
```cpp
// ✗ BAD
std::cout << "Entity count: " << count << std::endl;

// ✓ GOOD
LOG_INFO("Entity count: " << count);
LOG_ERROR("Failed to load: " << filename);
LOG_DEBUG("Update time: " << deltaTime);
```

**Quality Gate:** ✓ No raw console output (use Logger)

### 6. Copyright & Legal Compliance

**Check Command:**
```bash
# Find files missing copyright header
find src/ include/ -type f \( -name "*.cpp" -o -name "*.hpp" \) -exec grep -L "Copyright (c) 2025 Hammer Forged Games" {} \;
```

**Required Header:**
```cpp
/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/
```

**Quality Gate:** ✓ All source files have copyright header

### 7. Test Coverage

**Check Command:**
```bash
# For modified files, check if corresponding test exists
# Example: if src/managers/NewManager.cpp exists, check for tests/NewManager_tests.cpp
```

**Rules:**
- New managers must have test file in `tests/`
- New managers must have test script in `tests/test_scripts/run_*_tests.sh`
- Test script must be added to `run_all_tests.sh`

**Quality Gate:** ✓ New code has corresponding tests

## Quality Report Format

```markdown
=== HAMMERENGINE QUALITY GATE REPORT ===
Generated: YYYY-MM-DD HH:MM:SS
Branch: <current-branch>

## Compilation Quality
✓/✗ Status: <PASSED/FAILED>
  Warnings: <count>
  Errors: <count>

<details if failures>

## Static Analysis (cppcheck)
✓/✗ Status: <PASSED/FAILED>
  Errors: <count>
  Warnings: <count>

<list of issues>

## Coding Standards
✓/✗ Naming Conventions: <PASSED/FAILED>
  <violations if any>

✓/✗ Formatting: <PASSED/FAILED>
  <violations if any>

## Threading Safety (CRITICAL)
✓/✗ Static Variables: <PASSED/FAILED>
  <violations - BLOCKING>

✓/✗ ThreadSystem Usage: <PASSED/FAILED>
  <violations if any>

✓/✗ Mutex Protection: <PASSED/FAILED>
  <violations if any>

## Architecture Compliance
✓/✗ Rendering Rules: <PASSED/FAILED>
✓/✗ RAII/Smart Pointers: <PASSED/FAILED>
✓/✗ Logger Usage: <PASSED/FAILED>

## Legal Compliance
✓/✗ Copyright Headers: <PASSED/FAILED>
  Missing: <count> files
  <list files>

## Test Coverage
✓/✗ Tests Exist: <PASSED/FAILED>
  <missing tests>

---
## OVERALL STATUS: ✓ PASSED / ✗ FAILED

✓ Ready to commit
✗ Fix <count> violations before commit

### Critical Issues (BLOCKING)
<list blocking issues>

### Warnings (Review Required)
<list warnings>

### Recommendations
<specific fixes>
```

## Exit Codes

- **0:** All checks passed
- **1:** Critical violations (static vars, threading issues)
- **2:** Compilation warnings/errors
- **3:** Static analysis failures
- **4:** Missing copyright headers
- **5:** Multiple categories failed

## Usage as Git Pre-Commit Hook

This Skill can be integrated as a git hook:

```bash
# .git/hooks/pre-commit
#!/bin/bash
# Ask Claude to run quality check
claude-code "run quality check on my changes"

if [ $? -ne 0 ]; then
    echo "Quality check failed. Fix issues before committing."
    exit 1
fi
```

## Usage Examples

When the user says:
- "check code quality"
- "run quality gate"
- "verify my code before commit"
- "make sure code follows standards"
- "check for threading violations"

Activate this Skill automatically.

## Performance Expectations

- **Compilation Check:** 10-30 seconds
- **Static Analysis:** 30-60 seconds
- **Standards Checks:** 5-10 seconds
- **Total:** ~1-2 minutes

## Quick Fix Guide

**Most Common Violations:**

1. **Unused parameters:**
   ```cpp
   void func([[maybe_unused]] int param) { }
   ```

2. **Static variable in threaded code:**
   ```cpp
   // Move to class member or use thread_local
   ```

3. **Missing copyright:**
   ```cpp
   /* Copyright (c) 2025 Hammer Forged Games
    * All rights reserved.
    * Licensed under the MIT License - see LICENSE file for details
   */
   ```

4. **Using std::cout:**
   ```cpp
   LOG_INFO("message");  // instead of std::cout
   ```

5. **Raw new/delete:**
   ```cpp
   auto ptr = std::make_unique<Type>();  // instead of new
   ```

## Integration with Workflow

Use this Skill:
- **Before every commit** - Catch issues early
- **During PR review** - Validate code quality
- **After merging** - Ensure standards maintained
- **When adding new systems** - Verify compliance

## Severity Classification

**BLOCKING (Must Fix):**
- Static variables in threaded code
- Compilation errors
- Critical cppcheck errors
- Missing copyright headers on new files

**WARNING (Should Fix):**
- Compilation warnings
- cppcheck warnings
- Naming convention violations
- Missing tests for new code

**INFO (Consider Fixing):**
- Style suggestions
- Performance hints
- Code organization recommendations
