# Claude Code Skills for HammerEngine Development

## Overview

Claude Code Skills are specialized automation tools designed to streamline common development workflows for the SDL3 HammerEngine project. These skills leverage AI analysis to automate tasks that would otherwise require manual investigation and documentation.

Skills are located in `.claude/skills/` and are invoked through the Claude Code interface—no manual script execution required.

---

## Available Skills

### 1. hammer-dependency-analyzer

**Purpose:** Verify dependency structure and architecture health

**When to use:**
- Monthly architecture reviews
- After major refactors or API changes
- When investigating compile-time slowdowns
- Before merging large feature branches
- To validate layer violations don't occur

**What it does:**
- Analyzes all source file dependencies
- Detects circular dependencies (critical bugs)
- Measures coupling between components
- Validates adherence to layered architecture:
  - **Core Layer**: Fundamental utilities (Logger, SIMDMath, Vector2D)
  - **Managers Layer**: System managers (AIManager, CollisionManager, etc.)
  - **GameStates Layer**: Game state implementations
  - **Entities Layer**: Game entity types
  - **Features**: AI behaviors, collision types, events
- Calculates architecture health score (0-100)
- Generates dependency graph visualizations
- Identifies header bloat and excessive coupling

**Example output:**
```
Architecture Health: 85/100 ✓

Layer Violations: 2 (GameState depends on Entity utilities)
Circular Dependencies: 0 ✓
Coupling Index: 42 (Good - below 50)
Header Dependencies: 127 (Average)

Top Offenders:
  - UIManager → 15 dependencies (complex but expected)
  - GameEngine → 12 dependencies (central coordinator)
  - AIManager → 10 dependencies (performance-critical)
```

**Key metrics:**
- **Health Score**: 100 = perfect (rare), 80+ = good, 60-80 = acceptable, <60 = needs refactoring
- **Circular Dependencies**: Should always be 0
- **Layer Violations**: Count of dependencies that cross architectural boundaries
- **Coupling Index**: Lower is better (measures interconnectedness)

---

### 2. hammer-memory-profiler

**Purpose:** Simplified memory profiling and leak detection for SDL3 HammerEngine

**Tools used:**
- Valgrind memcheck (full leak detection)
- AddressSanitizer (ASAN, faster than Valgrind)
- Massif (heap profiling and visualization)

**When to use:**
- After implementing performance-critical systems
- When tracking down mysterious slowdowns
- After memory-intensive features (particle systems, asset loading)
- Before shipping releases
- When investigating frame time spikes
- After threading changes (detect race conditions in memory access)

**What it does:**
- Runs application under Valgrind memcheck
- Detects:
  - Memory leaks (allocated but not freed)
  - Use-after-free errors
  - Buffer overflows/underflows
  - Accessing uninitialized memory
  - Invalid pointer operations
- Profiles heap allocation patterns with Massif
- Generates allocation hotspot reports
- Provides system-by-system memory breakdown:
  - **TextureManager** memory usage
  - **FontManager** cached fonts
  - **AIManager** entity data structures
  - **ParticleManager** active particles
  - **UIManager** component instances
- Recommends optimizations based on patterns

**Example workflow:**
```bash
# Run profiler (automatically invoked through Claude Code)
# 1. Compiles debug build with ASAN enabled
# 2. Executes game with memory tracking
# 3. Generates detailed leak report
# 4. Analyzes allocation patterns
# 5. Provides optimization recommendations

Memory Profile Results:
================================
Total Allocations: 1,247
Peak Heap Size: 124.5 MB
Leaked Memory: 0 bytes ✓

System Breakdown:
  TextureManager:  45.2 MB (36%)
  FontManager:     12.1 MB (10%)
  ParticleManager: 8.3 MB (7%)
  AIManager:       6.5 MB (5%)
  Other:          52.4 MB (42%)

Hotspots:
  1. TextureManager::loadTexture (1.2M allocations)
  2. UIManager::createButton (890K allocations)
  3. ParticleManager::update (450K allocations)

Recommendations:
  - Consider texture atlasing for TextureManager
  - UI component pooling could reduce allocations
```

**Build configuration:**
```bash
# ASAN build (faster, catches common errors)
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" \
  -DUSE_MOLD_LINKER=OFF

# Full Valgrind profile (slower, more thorough)
valgrind --leak-check=full --show-leak-kinds=all \
  ./bin/debug/SDL3_Template
```

---

### 3. hammer-changelog-generator

**Purpose:** Generate comprehensive, professionally-formatted changelogs from git history

**When to use:**
- Preparing releases
- Documenting milestone updates
- Creating World Update documentation
- Summarizing features for stakeholders
- Before pushing major branches to main

**What it does:**
- Analyzes git history between branches/tags
- Extracts commit messages and code changes
- Runs test suite to validate changes
- Includes architect code review
- Generates professional markdown documentation
- Categorizes changes:
  - **Features**: New functionality (✨)
  - **Enhancements**: Improvements to existing features (🎯)
  - **Bug Fixes**: Issue resolutions (🐛)
  - **Performance**: Optimization improvements (⚡)
  - **Refactoring**: Code quality improvements (♻️)
  - **Documentation**: Doc updates (📚)
  - **Tests**: Test coverage additions (✅)
- Follows World Update format (major/minor/patch organization)

**Example output:**
```markdown
# World Update - Build 2025.11.20

## New Features ✨
- Triple buffering system with F3 toggle (Debug mode)
  - Configurable double/triple buffering
  - Buffer telemetry statistics
- 68+ test executables (45+ new tests)
  - SIMD correctness validation
  - Rendering pipeline tests
  - UI functional tests

## Enhancements 🎯
- UIConstants centralization (140+ UI constants)
  - Baseline resolution system (1920×1080)
  - Z-order layering standardization
  - Small screen support (1280×720)
- ThreadSanitizer integration
  - 110 benign race suppressions
  - Lock-free data structure validation

## Bug Fixes 🐛
- Player world bounds collision (fixes #234)
- Overlay repositioning on fullscreen toggle
- Event log positioning in dynamic layouts

## Performance ⚡
- SIMD benchmark suite (validates 2-4x speedups)
  - ParticleManager: 4.8x actual speedup
  - AIManager: 2.5-3.5x on ARM64
- Buffer reuse patterns validated

## Architecture 📐
- Dependency analysis: 85/100 health ✓
- Layer violation fixes: 2→0
- No circular dependencies

## Testing ✅
- 11 new test executables
- 22 new test scripts (bash + bat)
- Full integration test coverage

---
**Commits**: 42
**Files Changed**: 98
**Lines Added**: 17,065
**Lines Deleted**: 967
**Authors**: 1
**Duration**: 8 days
```

---

## Development Workflow Integration

### Monthly Architecture Review
```
1. Run hammer-dependency-analyzer
   - Check health score (target: 80+)
   - Review any new layer violations
   - Identify coupling hotspots

2. Address issues found
   - Refactor violations
   - Reduce coupling in high-index components
   - Update documentation

3. Run hammer-changelog-generator
   - Document changes for team
   - Prepare release notes
```

### Performance-Critical Feature Implementation
```
1. Implement feature (AI, particles, UI, etc.)
2. Add comprehensive tests
3. Run hammer-memory-profiler
   - Verify no leaks
   - Check allocation patterns
   - Identify optimization opportunities
4. Run hammer-dependency-analyzer
   - Ensure no new circular dependencies
   - Validate proper layer placement
5. Run benchmark regression suite
   - Compare before/after performance
```

### Before Release
```
1. Complete all feature implementation and testing
2. Run hammer-changelog-generator
   - Generate comprehensive release notes
   - Document all changes
   - Prepare for announcement
3. Run hammer-memory-profiler
   - Final memory leak validation
   - Peak heap check
4. Run hammer-dependency-analyzer
   - Final architecture verification
5. Merge to main and tag release
```

---

## Skill Configuration

Skills are configured in `.claude/skills/` with minimal setup:

```yaml
# Each skill has:
- README.md          # Documentation and usage
- scripts/          # Implementation details
- config.yaml       # Runtime configuration (optional)
```

### Usage Pattern

Skills are invoked through Claude Code interface:

```
User: "Can you run the dependency analyzer?"
Claude Code invokes: hammer-dependency-analyzer skill
Skill analyzes codebase and returns detailed report
```

No command-line invocation required—all interaction through Claude Code chat.

---

## Integration Recommendations

### Git Hooks

Consider running the dependency analyzer before commits:

```bash
# .git/hooks/pre-commit (optional)
# Prevents merging code that violates architecture constraints
#!/bin/bash
claude-code invoke hammer-dependency-analyzer --strict
exit_code=$?

if [ $exit_code -ne 0 ]; then
    echo "Architecture validation failed. Fix issues before committing."
    exit 1
fi
```

### CI/CD Pipeline

In GitHub Actions or similar:

```yaml
name: Pre-Merge Validation
on: [pull_request]

jobs:
  architecture:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Architecture Check
        run: |
          claude-code invoke hammer-dependency-analyzer --fail-on-violations

  memory-profile:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Memory Profiling
        run: |
          cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DUSE_ASAN=ON
          ninja -C build
          claude-code invoke hammer-memory-profiler
```

---

## Quick Reference

| Skill | Use Case | Frequency | Time |
|-------|----------|-----------|------|
| **dependency-analyzer** | Architecture health check | Monthly | 2-5 min |
| **memory-profiler** | Memory leak/allocation validation | After perf changes | 5-15 min |
| **changelog-generator** | Release documentation | Per release | 3-10 min |

---

## Troubleshooting

### Skill Invocation Issues

**Problem**: "Skill not found"
**Solution**: Ensure `.claude/skills/` directory exists with subdirectories for each skill

**Problem**: "Timeout during analysis"
**Solution**: Larger codebases may take longer; consider narrowing scope (e.g., analyze `src/` only)

**Problem**: "Memory profiler shows high false positives"
**Solution**: Ensure SDL3 libraries are built with debug symbols; use ASAN for faster, more accurate results

---

## See Also

- [CLAUDE.md](../../CLAUDE.md) - Project guidelines for Claude Code development
- [Dependency Analysis Results](../../docs/architecture/dependency_analysis_2025-11-17.md)
- [Performance Guide](../../docs/performance/PerformanceGuide.md)
- [Testing Guide](../../tests/TESTING.md)
