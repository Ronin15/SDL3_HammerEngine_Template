# HammerEngine Dependency Analysis Report

**Generated:** 2025-11-17 07:18:47
**Branch:** testing_update
**Commit:** 4a4f342
**Analysis Mode:** Full Architecture Audit

---

## Executive Summary

**Architecture Health Score:** 85.0/100 (Good)

**Status:** ✅ HEALTHY

**Key Findings:**
- No circular dependencies detected - clean acyclic include hierarchy
- No layer violations - all components respect architectural boundaries
- 9 tight coupling issues between managers requiring attention
- Low dependency depth (max 4) - minimal recompilation cascade risk
- 20 forward declaration opportunities for compile time optimization

**Overall Assessment:** HammerEngine demonstrates strong architectural health with excellent layer separation and no circular dependencies. The primary concern is tight coupling between certain managers (AIManager, CollisionManager, EventManager, WorldManager), which should be addressed through interface extraction or event-based communication. Header bloat is minimal (7.7% of headers), and dependency depths are very low, indicating good compilation efficiency.

---

## Dependency Statistics

**Codebase Size:**
- Total headers analyzed: 96
- Core layer: 7 files
- Managers layer: 19 files
- States layer: 12 files
- Entities layer: 12 files
- Utils layer: 7 files
- AI layer: 18 files
- Events layer: 14 files
- Collisions layer: 5 files
- World layer: 2 files

**Dependency Metrics:**
- Total dependencies: 163 edges
- Files with dependencies: 76
- Average dependencies per file: 1.79
- Max dependencies (single file): 6
- Circular dependencies: 0 ✅

---

## Circular Dependencies (CRITICAL)

✅ **NO CIRCULAR DEPENDENCIES DETECTED**

All include hierarchies are acyclic. Compilation order is deterministic.

**Analysis Results:**
- Nodes analyzed: 91
- Edges analyzed: 163
- Cycles found: 0

This is an excellent result indicating clean dependency management and proper use of forward declarations where needed.

---

## Coupling Analysis

### High-Coupling Components (Top 10)

| Component | Fan-Out | Fan-In | Instability | Status |
|-----------|---------|--------|-------------|--------|
| EventDemoState.hpp | 6 | 0 | 1.00 | 🟡 MODERATE |
| CollisionManager.hpp | 6 | 0 | 1.00 | 🟡 MODERATE |
| Vector2D.hpp | 0 | 36 | 0.00 | ⭐ CORE |
| Entity.hpp | 2 | 14 | 0.12 | 📦 STABLE |
| ResourceHandle.hpp | 0 | 13 | 0.00 | 📦 STABLE |
| GameState.hpp | 0 | 12 | 0.00 | 📦 STABLE |
| Event.hpp | 1 | 12 | 0.08 | 📦 STABLE |
| AIBehavior.hpp | 2 | 9 | 0.18 | 🔧 UTILITY |
| Resource.hpp | 0 | 7 | 0.00 | 🔧 UTILITY |
| WorkerBudget.hpp | 1 | 4 | 0.20 | 📄 LEAF |

**Legend:**
- **Fan-Out:** Number of dependencies (efferent coupling)
- **Fan-In:** Number of dependents (afferent coupling)
- **Instability:** Fan-Out / (Fan-In + Fan-Out)
  - 0.0 = Maximally stable (hard to change)
  - 1.0 = Maximally unstable (easy to change)

**Key Observations:**
- Vector2D.hpp is a core component (36 dependents) - changes require extensive recompilation
- Entity.hpp and Event.hpp are stable base classes with appropriate fan-in
- State classes have higher fan-out (expected pattern for application layer)
- No components show excessive coupling (no fan-out > 10)

### Manager-to-Manager Coupling

**Coupling Matrix:**

```
Manager                       AIManager   CollisionM  EntityStat  EventManag  FontManage  GameStateM  InputManag  ParticleMa  Pathfinder  ResourceFa  ResourceTe  SaveGameMa  SettingsMa  SoundManag  TextureMan  UIConstant  UIManager   WorldManag  WorldResou
AIManager                     -           ✓
CollisionManager                          -                       ✓
EntityStateManager                                    -
EventManager                                                      -
FontManager                                                                   -
GameStateManager                                                                          -
InputManager                                                                                          -
ParticleManager                                                                                                   -
PathfinderManager                                                 ✓                                                           -
ResourceFactory                                                                                                                           -
ResourceTemplateManager                                                                                                                               -
SaveGameManager                                                                                                                                                   -
SettingsManager                                                                                                                                                               -
SoundManager                                                                                                                                                                              -
TextureManager                                                                                                                                                                                        -
UIConstants                                                                                                                                                                                                       -
UIManager                                                                                                                                                                                                         ✓           -
WorldManager                                                      ✓                                                                                                                                                                       -
WorldResourceManager                                                                                                                                                                                                                                  -
```

Legend: ✓ = Direct dependency, - = Self, (blank) = No dependency

### Coupling Strength Analysis

**Tight Coupling Issues (🔴 CRITICAL):**

1. **AIManager → CollisionManager** (19 references)
   - **Status:** 🔴 TIGHT
   - **Impact:** AIManager heavily coupled to collision detection
   - **Recommendation:** Extract ICollisionQuery interface for spatial queries
   - **Benefit:** Allows AI to query collisions without tight binding

2. **AIManager → PathfinderManager** (20 references)
   - **Status:** 🔴 TIGHT
   - **Impact:** AI system tightly bound to pathfinding implementation
   - **Recommendation:** Already using PathfinderManager as interface - review if further abstraction needed
   - **Benefit:** Easier to swap pathfinding algorithms

3. **CollisionManager → EventManager** (15 references)
   - **Status:** 🔴 TIGHT
   - **Impact:** Collision system heavily uses event dispatch
   - **Recommendation:** Good pattern - events are appropriate for collision notifications
   - **Action:** No change needed (this is appropriate coupling)

4. **CollisionManager → WorldManager** (11 references)
   - **Status:** 🔴 TIGHT
   - **Impact:** Collision system queries world geometry
   - **Recommendation:** Extract IWorldQuery interface for tile/trigger queries
   - **Benefit:** Decouples collision from world implementation

5. **ResourceFactory → ResourceTemplateManager** (14 references)
   - **Status:** 🔴 TIGHT
   - **Impact:** Resource instantiation depends on templates
   - **Recommendation:** This coupling is inherent to factory pattern - acceptable
   - **Action:** No change needed (appropriate pattern)

6. **UIManager → FontManager** (13 references)
   - **Status:** 🔴 TIGHT
   - **Impact:** UI rendering depends on font system
   - **Recommendation:** Already using manager pattern - coupling is appropriate
   - **Action:** No change needed (UI needs fonts)

7. **UIManager → UIConstants** (29 references)
   - **Status:** 🔴 TIGHT
   - **Impact:** UI heavily uses shared constants
   - **Recommendation:** Constants file is appropriate pattern for UI consistency
   - **Action:** No change needed (good use of constants)

8. **WorldManager → EventManager** (17 references)
   - **Status:** 🔴 TIGHT
   - **Impact:** World system uses events for triggers/notifications
   - **Recommendation:** Good pattern - events appropriate for world interactions
   - **Action:** No change needed (appropriate coupling)

9. **WorldManager → WorldResourceManager** (13 references)
   - **Status:** 🔴 TIGHT
   - **Impact:** World system depends on resource loading
   - **Recommendation:** Consider injecting resource manager via GameEngine
   - **Benefit:** Clearer ownership and testability

**Moderate Coupling (⚠️  REVIEW):**

- CollisionManager → AIManager (7 references) - Review necessity
- InputManager → FontManager (6 references) - Review if needed for input display
- InputManager → UIManager (7 references) - Expected for input routing
- PathfinderManager → EventManager (6 references) - Events for pathfinding results
- ResourceTemplateManager → ResourceFactory (7 references) - Bidirectional, review if avoidable
- UIManager → InputManager (6 references) - Expected for UI interaction
- WorldManager → TextureManager (6 references) - Review if world needs direct texture access
- WorldResourceManager → EventManager (8 references) - Events for resource loading

**Recommendations:**

1. **Priority 1 (High Impact):**
   - Extract ICollisionQuery interface for AIManager
   - Extract IWorldQuery interface for CollisionManager

2. **Priority 2 (Medium Impact):**
   - Review bidirectional coupling between ResourceFactory and ResourceTemplateManager
   - Consider dependency injection for WorldManager → WorldResourceManager

3. **Priority 3 (Low Priority):**
   - Review moderate couplings for necessity
   - Document intentional tight couplings (UI, Events, Constants)

---

## Layer Violations

### Layer Integrity Check

**Core Layer** (7 files): ✅ CLEAN
Should depend on nothing.

**Managers Layer** (19 files): ✅ CLEAN
Should depend on Core, Utils, Events, AI, Entities, Collisions, World only.

**States Layer** (12 files): ✅ CLEAN
Should depend on Core, Managers, Utils, Entities, Events, AI, Collisions, World.
No cross-state dependencies detected.

**Entities Layer** (12 files): ✅ CLEAN
Should depend on Core, Utils, Events only.

**Utils Layer** (7 files): ✅ CLEAN
Should be dependency-free (pure utilities).

**AI Layer** (18 files): ✅ CLEAN
Should depend on Core, Utils, Events only.

**Events Layer** (14 files): ✅ CLEAN
Should depend on Core, Utils only.

**Collisions Layer** (5 files): ✅ CLEAN
Should depend on Core, Utils only.

**World Layer** (2 files): ✅ CLEAN
Should depend on Core, Utils, Events only.

### Violation Details

✅ **NO LAYER VIOLATIONS**

All components respect layered architecture boundaries. This indicates:
- Clean separation of concerns
- Proper dependency direction (high-level depends on low-level)
- No upward dependencies that violate architecture
- States properly isolated from each other
- Managers don't depend on application-layer States

**Architectural Compliance:** Excellent

---

## Header Bloat Analysis

### High-Include Headers

| Header | #Includes | Status | Dependents |
|--------|-----------|--------|-----------|
| EventManager.hpp | 19 | 🔴 HIGH | 4 files |
| ThreadSystem.hpp | 16 | 🔴 HIGH | ? files |
| CollisionManager.hpp | 16 | 🔴 HIGH | 1 file |
| ParticleManager.hpp | 15 | ⚠️  MODERATE | ? files |
| AIManager.hpp | 13 | ⚠️  MODERATE | 1 file |
| PathfinderManager.hpp | 12 | ⚠️  MODERATE | 1 file |
| HierarchicalSpatialHash.hpp | 11 | ⚠️  MODERATE | 1 file |

**Bloat Analysis:**
- 7 headers with high include counts (7.7% of codebase)
- Most high-bloat headers are Managers (expected due to coordination role)
- Low percentage indicates good include hygiene overall

**Bloat Amplification:**

Headers with many includes that are widely used cause compilation ripple effects.

**High-Risk Header:**
- **EventManager.hpp** - 19 includes, used by 4 files
  - Ripple effect: ~76 transitive includes per dependent
  - **Recommendation:** Consider forward declarations for event types
  - **Benefit:** Reduce compilation cascade when event system changes

**Other Bloat Sources:**
- ThreadSystem.hpp (16 includes) - Core threading infrastructure
- CollisionManager.hpp (16 includes) - Large subsystem with many components

**Overall Assessment:** Header bloat is well-controlled. Managers naturally have higher include counts due to their coordination role.

### Forward Declaration Opportunities

**Top 20 Opportunities:**

1. **Vector2D.hpp forward declarations** (12 opportunities)
   - Can be forward-declared in: WanderBehavior, RequestQueue, SpatialPriority, DroppedItem, NPC, Player, WorldTriggerEvent, EventManager, InputManager, SaveGameManager, UIManager, BinarySerializer
   - **Impact:** Vector2D is used by 36 files - significant compilation savings
   - **Challenge:** Vector2D is a simple struct often used by value
   - **Recommendation:** Consider moving Vector2D-heavy code to .cpp files

2. **Config class forward declarations** (3 opportunities)
   - AttackBehaviorConfig, InventoryComponent
   - **Impact:** Reduces header dependencies for behavior and component systems
   - **Benefit:** Faster compilation for behavior changes

3. **Manager forward declarations** (3 opportunities)
   - GameStateManager, TimestepManager
   - **Impact:** Core engine headers can use forward declarations
   - **Benefit:** Reduces GameEngine.hpp include weight

4. **Data structure forward declarations** (2 opportunities)
   - WorldData, CollisionInfo
   - **Impact:** World and collision systems can use pointers/references
   - **Benefit:** Isolates data structure changes

**Implementation Pattern:**

```cpp
// Before (in .hpp)
#include "Vector2D.hpp"

class MyClass {
    Vector2D m_position;  // Member variable (needs full definition)
};

// After (if only using pointers/references)
class Vector2D;  // Forward declaration

class MyClass {
    Vector2D* m_position;  // Pointer (forward declaration OK)
    // Move #include "Vector2D.hpp" to MyClass.cpp
};
```

**Estimated Compile Time Savings:** ~10% reduction with aggressive forward declaration usage

**Recommendation:** Focus on high-fan-in headers (Vector2D, Entity) for maximum impact.

---

## Dependency Depth Analysis

### Compile Time Impact

| Header | Depth | Impact | Recommendation |
|--------|-------|--------|----------------|
| AIManager.hpp | 4 | ✅ LOW | Acceptable |
| EventDemoState.hpp | 4 | ✅ LOW | Acceptable |
| CollisionManager.hpp | 3 | ✅ LOW | Acceptable |
| WorldManager.hpp | 3 | ✅ LOW | Acceptable |
| All others | ≤3 | ✅ LOW | Excellent |

**Total Dependency Depth:** 150
**Average Depth:** 1.6
**Maximum Depth:** 4

**Distribution:**
- 🔴 Very High (>10): 0 headers
- ⚠️  High (8-10): 0 headers
- 🟡 Moderate (5-7): 0 headers
- ✅ Low (0-4): 91 headers (100%)

**Analysis:**

This is an **excellent result**. Maximum depth of 4 means:
- Changing any header causes at most 4 levels of recompilation cascade
- Most headers have depth 0-2 (leaf or near-leaf nodes)
- Average depth of 1.6 indicates shallow dependency trees

**Comparison with Industry Standards:**
- Large codebases often have depths of 10-15 (problematic)
- Well-architected systems aim for depths < 7
- HammerEngine achieves depths ≤ 4 (exceptional)

**Why This Matters:**
- Low depth = fast incremental compilation
- Changing Vector2D.hpp (depth 0) doesn't cascade far
- Manager changes (depth 3-4) have limited ripple effect

**Recommendations:**
- ✅ Continue current architecture patterns
- ✅ Maintain shallow include hierarchies
- ✅ Use forward declarations to prevent depth increases

---

## Architecture Health Scorecard

| Category | Score | Weight | Weighted | Status |
|----------|-------|--------|----------|--------|
| Circular Dependencies | 10.0 | 30% | 30.0 | ✅ |
| Layer Compliance | 10.0 | 25% | 25.0 | ✅ |
| Coupling Strength | 4.0 | 20% | 8.0 | 🔴 |
| Header Bloat | 8.0 | 15% | 12.0 | ✅ |
| Dependency Depth | 10.0 | 10% | 10.0 | ✅ |
| **TOTAL** | | **100%** | **85.0** | **A** |

**Grading Scale:**
- 90-100: A+ (Excellent architecture)
- **80-89: A (Good architecture, minor issues)** ← HammerEngine
- 70-79: B (Fair architecture, needs improvement)
- 60-69: C (Poor architecture, refactoring required)
- Below 60: F (Critical issues, major refactoring needed)

**Score Breakdown:**

1. **Circular Dependencies (30%):** Perfect Score (10/10)
   - Zero circular dependencies detected
   - Clean acyclic dependency graph
   - No compilation order issues

2. **Layer Compliance (25%):** Perfect Score (10/10)
   - Zero layer violations across all layers
   - Core, Utils remain pure/foundation layers
   - Managers don't depend on States
   - States properly isolated from each other

3. **Coupling Strength (20%):** Needs Improvement (4/10)
   - 9 tight coupling instances between managers
   - Some appropriate (EventManager usage, UI → Font)
   - Some need refactoring (AIManager → CollisionManager)
   - **Primary area for improvement**

4. **Header Bloat (15%):** Good (8/10)
   - Only 7.7% of headers have high include counts
   - EventManager.hpp highest at 19 includes
   - Most managers well-controlled
   - Forward declaration opportunities exist

5. **Dependency Depth (10%):** Perfect Score (10/10)
   - Maximum depth of 4 (excellent)
   - Average depth of 1.6 (exceptional)
   - Shallow dependency trees throughout
   - Fast incremental compilation

**Overall Assessment:**

HammerEngine achieves a **Good (A)** architecture health score of **85/100**. The codebase demonstrates excellent structural discipline with zero circular dependencies, zero layer violations, and very low dependency depths. The primary opportunity for improvement is reducing tight coupling between certain managers through interface extraction and dependency injection patterns.

---

## Recommendations

### Critical (Fix Immediately)

None. No critical architectural issues detected.

### Important (Address Soon)

**1. Reduce Manager Coupling (Priority: HIGH, Effort: 4-8 hours)**

**Issue:** 9 tight coupling instances, primarily AIManager ↔ CollisionManager

**Specific Actions:**

a) **AIManager → CollisionManager** (19 references)
   ```cpp
   // Create ICollisionQuery interface
   class ICollisionQuery {
   public:
       virtual ~ICollisionQuery() = default;
       virtual bool checkCollision(const AABB& bounds) const = 0;
       virtual std::vector<Entity*> queryRadius(Vector2D pos, float radius) const = 0;
   };

   // AIManager depends on interface, not concrete class
   class AIManager {
       ICollisionQuery* m_collisionQuery;  // Not CollisionManager*
   };

   // GameEngine wires concrete implementation
   aiManager->setCollisionQuery(collisionManager);
   ```
   - **Benefit:** Allows AI changes without recompiling collision code
   - **Testing:** Easier to mock collision for AI tests

b) **CollisionManager → WorldManager** (11 references)
   ```cpp
   // Create IWorldQuery interface for tile/trigger queries
   class IWorldQuery {
   public:
       virtual ~IWorldQuery() = default;
       virtual TileType getTile(Vector2D pos) const = 0;
       virtual std::vector<Trigger> getTriggersInBounds(const AABB& bounds) const = 0;
   };
   ```
   - **Benefit:** Collision system independent of world implementation
   - **Testing:** Can test collision without full world system

c) **WorldManager → WorldResourceManager** (13 references)
   ```cpp
   // Inject via GameEngine instead of direct coupling
   class WorldManager {
       WorldResourceManager* m_resourceMgr;  // Injected dependency

       void setResourceManager(WorldResourceManager* mgr) {
           m_resourceMgr = mgr;
       }
   };
   ```
   - **Benefit:** Clearer ownership, easier testing
   - **Pattern:** Dependency injection

**Estimated Impact:**
- Coupling score: 4/10 → 7/10
- Overall health score: 85/100 → 91/100 (A → A+)

**2. Optimize High-Include Headers (Priority: MEDIUM, Effort: 2-3 hours)**

**Issue:** EventManager.hpp (19 includes), ThreadSystem.hpp (16 includes), CollisionManager.hpp (16 includes)

**Specific Actions:**

a) **EventManager.hpp** (19 includes with 4 dependents)
   ```cpp
   // Split into EventManager.hpp (interface) + EventManagerImpl.hpp (implementation)

   // EventManager.hpp (minimal includes)
   #pragma once
   class Event;  // Forward declaration
   class EventTypeId;  // Forward declaration

   class EventManager {
       // Interface only
   };

   // EventManagerImpl.hpp (full includes)
   #include "EventManager.hpp"
   #include "Event.hpp"
   #include "EventTypeId.hpp"
   // ... other includes
   ```
   - **Benefit:** Files depending on EventManager don't need full event system
   - **Impact:** Reduces ripple effect from 19 includes to ~3 includes

b) **Apply Forward Declarations** (see Forward Declaration Opportunities section)
   - Focus on Vector2D (36 dependents)
   - Move includes to .cpp files where possible

**Estimated Impact:**
- Header bloat score: 8/10 → 9/10
- Compile time improvement: ~10-15%

### Optional (Consider)

**3. Document Intentional Tight Coupling (Priority: LOW, Effort: 1 hour)**

**Issue:** Some tight coupling is intentional and appropriate (UI → Font, Manager → EventManager)

**Action:** Add architecture documentation:

```markdown
## Intentional Manager Coupling

The following manager dependencies are intentional design decisions:

1. **UIManager → FontManager** (13 refs)
   - Reason: UI rendering requires font system
   - Pattern: Manager dependency injection
   - Status: ✅ Acceptable

2. **Managers → EventManager** (multiple)
   - Reason: Event-driven architecture for decoupling
   - Pattern: Event bus / pub-sub
   - Status: ✅ Acceptable

3. **UIManager → UIConstants** (29 refs)
   - Reason: Shared UI styling and layout constants
   - Pattern: Constants file for consistency
   - Status: ✅ Acceptable
```

**Benefit:** Clarifies architectural intent, prevents future "fixes" of good patterns

**4. Establish Pre-Commit Dependency Checks (Priority: LOW, Effort: 2-3 hours)**

**Action:** Add git pre-commit hook to detect regressions:

```bash
#!/bin/bash
# .git/hooks/pre-commit

# Run dependency analysis
python3 test_results/dependency_analysis/detect_cycles.py

if [ $? -ne 0 ]; then
    echo "❌ Circular dependencies detected - commit blocked"
    exit 1
fi

# Check layer violations
python3 test_results/dependency_analysis/detect_layer_violations.py | grep "🔴"

if [ $? -eq 0 ]; then
    echo "⚠️  Layer violations detected - review recommended"
    # Don't block, just warn
fi
```

**Benefit:** Catch architectural regressions before they enter codebase

---

## Dependency Visualizations

### GameEngine Dependency Tree

```
└── GameEngine.hpp
    └── GameStateManager.hpp
        └── GameState.hpp
```

**Analysis:** GameEngine has minimal dependencies (only GameStateManager). This is excellent design - the core engine class is lightweight and delegates to managers.

### AIManager Dependency Tree

```
└── AIManager.hpp
│   └── AIBehavior.hpp
│   │   └── Entity.hpp
│   │   │   └── UniqueID.hpp
│   │       └── Vector2D.hpp
│       └── Vector2D.hpp
│   └── WorkerBudget.hpp
│       └── Logger.hpp
│   └── Entity.hpp
│   │   └── UniqueID.hpp
│       └── Vector2D.hpp
    └── CollisionManager.hpp (tight coupling)
    │   └── Entity.hpp
    │   └── CollisionBody.hpp
    │   └── CollisionInfo.hpp
    │   └── HierarchicalSpatialHash.hpp
    │   └── TriggerTag.hpp
        └── EventManager.hpp
```

**Analysis:** AIManager directly includes CollisionManager (tight coupling issue). Dependencies fan out to Entity, Vector2D, and threading primitives (appropriate). Depth of 4 is acceptable.

### CollisionManager Dependency Tree

```
└── CollisionManager.hpp
│   └── Entity.hpp
│   └── CollisionBody.hpp
│   └── CollisionInfo.hpp
│   └── HierarchicalSpatialHash.hpp
│   └── TriggerTag.hpp
    └── EventManager.hpp
```

**Analysis:** CollisionManager has dependencies on collision primitives (appropriate) and EventManager (good pattern for notifications). Relatively shallow tree.

### EventManager Dependency Tree

```
└── EventManager.hpp
│   └── WorkerBudget.hpp
│       └── Logger.hpp
│   └── EventTypeId.hpp
│   └── ResourceHandle.hpp
    └── Vector2D.hpp
```

**Analysis:** EventManager dependencies are minimal and appropriate (threading, logging, type IDs). Good design for a central coordination component.

### WorldManager Dependency Tree

```
└── WorldManager.hpp
│   └── WorldData.hpp
│   └── WorldGenerator.hpp
│   └── ResourceHandle.hpp
    └── EventManager.hpp
```

**Analysis:** WorldManager depends on world data structures and resource system. EventManager dependency is appropriate for world events/triggers.

---

## Files Requiring Attention

Based on analysis, these files need modification:

### High Priority

1. **include/managers/AIManager.hpp**
   - Issue: Tight coupling to CollisionManager (19 references)
   - Action: Extract ICollisionQuery interface
   - Impact: HIGH - improves testability and compilation time

2. **include/managers/CollisionManager.hpp**
   - Issue: Tight coupling to WorldManager (11 references)
   - Action: Extract IWorldQuery interface
   - Impact: HIGH - decouples collision from world implementation

### Medium Priority

3. **include/managers/EventManager.hpp**
   - Issue: High include count (19 includes)
   - Action: Consider header split or forward declarations
   - Impact: MEDIUM - reduces compilation ripple effect

4. **include/managers/WorldManager.hpp**
   - Issue: Tight coupling to WorldResourceManager (13 references)
   - Action: Use dependency injection
   - Impact: MEDIUM - clearer ownership

### Low Priority

5. **Multiple files with Vector2D.hpp includes** (20 files)
   - Issue: Can use forward declaration
   - Action: Forward-declare Vector2D, move include to .cpp
   - Impact: LOW per file, HIGH cumulative
   - Challenge: Vector2D often used by value (needs full definition)

---

## Next Steps

1. **Review this report** with team/architect
2. **Prioritize fixes** based on recommendations above
3. **Create tickets** for identified issues:
   - Ticket 1: Extract ICollisionQuery interface for AIManager
   - Ticket 2: Extract IWorldQuery interface for CollisionManager
   - Ticket 3: Optimize EventManager.hpp includes
   - Ticket 4: Implement dependency injection for WorldManager
4. **Re-run analysis** after fixes to verify improvements
5. **Schedule regular audits** (monthly recommended)

**Re-run Analysis:**

```bash
# After fixes, verify improvements
python3 test_results/dependency_analysis/extract_deps.py
python3 test_results/dependency_analysis/detect_cycles.py test_results/dependency_analysis/dependency_graph.txt
python3 test_results/dependency_analysis/analyze_coupling.py test_results/dependency_analysis/dependency_graph.txt .
python3 test_results/dependency_analysis/calc_health_score.py
```

Or use the hammer-dependency-analyzer skill:

```bash
# Re-run full analysis
# In Claude Code, say: "analyze dependencies"
```

---

## Comparison with Previous Analysis

*No previous analysis baseline available. This is the initial architectural audit.*

**Recommendation:** Save this report as baseline for future comparisons.

**Baseline Metrics:**
- Health Score: 85/100
- Circular Dependencies: 0
- Layer Violations: 0
- Tight Coupling: 9 instances
- Max Depth: 4
- High-Bloat Headers: 7

Future analyses will compare against these metrics to track architectural trends.

---

## Conclusion

HammerEngine demonstrates **strong architectural health** with excellent separation of concerns, clean layering, and no circular dependencies. The codebase follows modern C++ best practices with appropriate use of managers, event-driven architecture, and threading patterns.

**Strengths:**
- ✅ Zero circular dependencies
- ✅ Perfect layer compliance
- ✅ Very low dependency depths (max 4)
- ✅ Controlled header bloat (7.7%)
- ✅ Good use of forward declarations already

**Areas for Improvement:**
- 🔴 Reduce tight coupling between managers (priority: high)
- ⚠️  Optimize high-include headers (priority: medium)
- 📝 Document intentional coupling patterns (priority: low)

**Impact of Recommended Changes:**
- Health score: 85/100 → 91/100 (A → A+)
- Coupling score: 4/10 → 7/10 (significant improvement)
- Compile time: ~10-15% faster (with optimizations)

The architecture provides a solid foundation for continued development. Addressing the identified coupling issues will further improve maintainability, testability, and compilation efficiency.

---

**Report Generated By:** hammer-dependency-analyzer Skill
**Report Saved To:** `docs/architecture/dependency_analysis_2025-11-17.md`
**Analysis Duration:** ~18 minutes
**Scripts Generated:** 7 Python analysis tools in `test_results/dependency_analysis/`
