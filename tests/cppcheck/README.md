# Cppcheck Configuration for VoidLight-Framework

This directory contains the cppcheck configuration and scripts used by the repository's static-analysis workflow.

## Quick Start

### Linux/macOS
```bash
cd tests/cppcheck
./cppcheck_focused.sh
```

### Windows
```bat
cd tests\cppcheck
cppcheck_focused.bat
```

## Configuration

### `cppcheck_lib.cfg`
Project-specific library definitions that keep cppcheck aligned with the codebase's macros and helper APIs.

There is no separate suppressions list in the current checkout. The focused and full scripts rely on `cppcheck_lib.cfg` plus the script-level filters they already apply.

## Scripts

- `run_cppcheck.sh` - Full analysis for Linux/macOS
- `run_cppcheck.bat` - Full analysis for Windows
- `cppcheck_focused.sh` - Focused analysis for Linux/macOS
- `cppcheck_focused.bat` - Focused analysis for Windows

## Output

Full scripts write timestamped XML and text summaries to `../../test_results/`.
Focused scripts print a terminal summary and do not persist reports.

## Usage Examples

```bash
cd tests/cppcheck
./run_cppcheck.sh
./cppcheck_focused.sh
```

## Build Integration

```cmake
find_program(CPPCHECK cppcheck)
if(CPPCHECK)
    add_custom_target(cppcheck
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/cppcheck/run_cppcheck.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/cppcheck
        COMMENT "Running cppcheck analysis"
    )
endif()
```

## Maintenance

When project-specific macros or helper functions change:
1. Update `cppcheck_lib.cfg`
2. Re-run the focused analysis
3. Extend the library config only with targeted entries

If cppcheck starts reporting false positives, prefer a focused library-config update over broad suppressions.
