# HammerEngine Dependency Analysis Report

**Generated:** 2026-01-08
**Branch:** EDM_handles
**Analysis Mode:** Full Architecture Audit

---

## Executive Summary

**Architecture Health Score:** 92/100 (EXCELLENT)

**Status:** ✅ HEALTHY

**Key Findings:**
- ✅ No circular dependencies detected
- ✅ No actual layer violations (all cross-layer deps are acceptable patterns)
- ✅ Manager coupling is functional and appropriate for game engine
- ⚠️ Minor header bloat in 2 files (ThreadSystem.hpp, EventManager.hpp)

**Overall Assessment:** The HammerEngine architecture demonstrates excellent adherence to layered design principles. The EDM (EntityDataManager) migration has been implemented cleanly with proper separation of concerns. All detected cross-layer dependencies are legitimate architectural patterns (inheritance, orchestration, cross-cutting concerns).

---

## Dependency Statistics

**Codebase Size:**
| Layer | Files |
|-------|-------|
| AI | 14 |
| Collisions | 5 |
| Controllers | 6 |
| Core | 5 |
| Entities | 23 |
| Events | 17 |
| Managers | 21 |
| States | 12 |
| Utils | 7 |
| World | 2 |
| **Total** | **112** |

**Dependency Metrics:**
| Metric | Value |
|--------|-------|
| Total headers analyzed | 112 |
| Headers with dependencies | 91 |
| Circular dependencies | 0 ✅ |
| Layer violations | 0 ✅ |
| High-include headers (>15) | 2 |

---

## Circular Dependencies

✅ **NO CIRCULAR DEPENDENCIES DETECTED**

All include hierarchies are acyclic. Compilation order is deterministic.

---

## Layer Compliance

### Layer Rules Validated

| Layer | Status | Notes |
|-------|--------|-------|
| Core | ✅ CLEAN | GameEngine orchestrates managers (correct) |
| Managers | ✅ CLEAN | GameStateManager manages GameState (correct) |
| States | ✅ CLEAN | All inherit from GameState base (correct) |
| Entities | ✅ CLEAN | No violations |
| Utils | ✅ CLEAN | Logger cross-cutting allowed |

### Acceptable Patterns Detected

1. **Base Class Inheritance** - States inheriting from GameState.hpp
2. **Orchestrator Pattern** - GameEngine including manager headers
3. **Domain Management** - GameStateManager including GameState
4. **Cross-Cutting Concerns** - Logger used from Utils

All detected cross-layer dependencies serve legitimate architectural purposes.

---

## Coupling Analysis

### Manager-to-Manager Coupling

| Source | Target | Refs | Status |
|--------|--------|------|--------|
| UIManager | UIConstants | 118 | ✅ Constants header |
| CollisionManager | EntityDataManager | 25 | ✅ Functional |
| WorldManager | EventManager | 19 | ✅ Functional |
| CollisionManager | EventManager | 17 | ✅ Functional |
| ResourceFactory | ResourceTemplateManager | 14 | ✅ Factory pattern |
| GameTimeManager | EventManager | 14 | ✅ Event-driven |
| UIManager | FontManager | 13 | ✅ Functional |
| WorldManager | WorldResourceManager | 13 | ✅ Functional |
| BackgroundSimulationManager | EntityDataManager | 12 | ✅ Functional |
| AIManager | EntityDataManager | 11 | ✅ Functional |
| CollisionManager | WorldManager | 11 | ✅ Functional |
| AIManager | PathfinderManager | 9 | ✅ Functional |

**Summary:**
- Functional coupling (expected): 12 pairs
- Needs review: 0 pairs
- Total significant pairs: 25

**Assessment:** All manager coupling serves clear game system functionality. The EntityDataManager is correctly positioned as the central data authority with appropriate coupling from AI, Collision, and Background simulation systems.

---

## Header Bloat Analysis

### High Include Count Headers

| Header | Local | System | Total | Status |
|--------|-------|--------|-------|--------|
| ThreadSystem.hpp | 1 | 16 | 17 | 🔴 HIGH |
| EventManager.hpp | 3 | 14 | 17 | 🔴 HIGH |
| CollisionManager.hpp | 6 | 9 | 15 | ⚠️ MODERATE |
| AIManager.hpp | 4 | 9 | 13 | ⚠️ MODERATE |
| EntityDataManager.hpp | 5 | 7 | 12 | ⚠️ MODERATE |
| PathfinderManager.hpp | 4 | 8 | 12 | ⚠️ MODERATE |

**Note:** High system include counts in ThreadSystem.hpp and EventManager.hpp are acceptable as these are foundational headers requiring STL threading/container support.

### Most Frequently Included Headers

| Header | Included By | Classification |
|--------|-------------|----------------|
| Logger.hpp | 56 files | ⭐ CORE |
| Vector2D.hpp | 42 files | ⭐ CORE |
| EntityDataManager.hpp | 29 files | ⭐ CORE (EDM) |
| EventManager.hpp | 20 files | 📦 STABLE |
| GameEngine.hpp | 20 files | 📦 STABLE |
| WorldManager.hpp | 18 files | 📦 STABLE |
| Entity.hpp | 17 files | 📦 STABLE |
| PathfinderManager.hpp | 17 files | 📦 STABLE |

**Assessment:** The EntityDataManager being included by 29 files confirms its role as the central data authority (correct EDM architecture).

---

## Dependency Depth Analysis

### Top Headers by Depth

| Header | Depth | Impact |
|--------|-------|--------|
| CollisionEvent.hpp | 4 | 🟡 MODERATE |
| AdvancedAIDemoState.hpp | 4 | 🟡 MODERATE |
| GamePlayState.hpp | 4 | 🟡 MODERATE |
| AIDemoState.hpp | 4 | 🟡 MODERATE |
| EventDemoState.hpp | 4 | 🟡 MODERATE |
| CollisionManager.hpp | 3 | ✅ LOW |
| PathfinderManager.hpp | 3 | ✅ LOW |
| AIManager.hpp | 3 | ✅ LOW |

**Assessment:** Maximum dependency depth of 4 is acceptable. No headers have excessive transitive dependencies.

---

## Fan-Out / Fan-In Analysis

### Highest Fan-Out (Dependencies)

| Header | Fan-Out | Assessment |
|--------|---------|------------|
| GamePlayState.hpp | 9 | State needs many managers |
| EventDemoState.hpp | 7 | Demo state |
| CollisionManager.hpp | 6 | System manager |
| AttackBehavior.hpp | 6 | Complex behavior |
| EntityDataManager.hpp | 5 | Core data manager |

### Highest Fan-In (Dependents)

| Header | Fan-In | Classification |
|--------|--------|----------------|
| Vector2D.hpp | 36 | ⭐ CORE utility |
| Event.hpp | 15 | 📦 Foundation |
| EntityHandle.hpp | 13 | 📦 EDM foundation |
| ResourceHandle.hpp | 13 | 📦 Resource system |
| Entity.hpp | 13 | 📦 Entity base |
| GameState.hpp | 12 | 📦 State base |
| EntityState.hpp | 12 | 📦 State component |

**Assessment:** High fan-in on foundation types (Vector2D, EntityHandle, Event) is expected and correct.

---

## Architecture Health Scorecard

| Category | Score | Weight | Weighted | Status |
|----------|-------|--------|----------|--------|
| Circular Dependencies | 10/10 | 30% | 3.0 | ✅ |
| Layer Compliance | 10/10 | 25% | 2.5 | ✅ |
| Coupling Strength | 9/10 | 20% | 1.8 | ✅ |
| Header Bloat | 8/10 | 15% | 1.2 | ⚠️ |
| Dependency Depth | 10/10 | 10% | 1.0 | ✅ |
| **TOTAL** | | **100%** | **92/100** | **A** |

**Grade: A (Excellent Architecture)**

---

## EDM Architecture Validation

The EntityDataManager (EDM) refactoring demonstrates correct architectural patterns:

1. **Central Data Authority** ✅
   - EntityDataManager included by 29 files
   - All major systems (AI, Collision, BackgroundSim) correctly access EDM

2. **Proper Layering** ✅
   - EDM in Managers layer
   - No upward dependencies to States

3. **Functional Coupling** ✅
   - AIManager → EntityDataManager (11 refs)
   - CollisionManager → EntityDataManager (25 refs)
   - BackgroundSimulationManager → EntityDataManager (12 refs)

4. **No Circular Dependencies** ✅
   - EDM graph is acyclic
   - Clean compilation order

---

## Recommendations

### No Critical Issues

The architecture is healthy. No immediate action required.

### Optional Improvements (Low Priority)

1. **Consider Forward Declarations**
   - ThreadSystem.hpp and EventManager.hpp have high system include counts
   - Could reduce compile times with forward declarations for complex types
   - Priority: LOW (only affects compile time, not architecture)

2. **Monitor EDM Growth**
   - EntityDataManager.hpp (12 includes) should be monitored
   - As more systems integrate, consider splitting into smaller headers
   - Priority: LOW (current size is acceptable)

---

## Conclusion

The HammerEngine codebase demonstrates **excellent architectural discipline**. The EDM migration has been implemented cleanly with:

- Zero circular dependencies
- Zero layer violations
- Appropriate functional coupling between game systems
- Reasonable header sizes

**Recommendation:** Continue current development practices. Run this analysis monthly to catch architectural drift early.

---

## Next Analysis

Schedule next full audit: 2026-02-08 (one month)

```bash
# Re-run analysis
# Use hammer-dependency-analyzer skill
```

---

**Report Generated By:** hammer-dependency-analyzer Skill
**Report Location:** `docs/architecture/dependency_analysis_2026-01-08.md`
