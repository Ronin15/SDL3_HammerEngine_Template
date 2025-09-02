# Pathfinding System Redesign

## Current Problems

### Architecture Issues
- **Individual entity timers**: Each NPC manages its own pathfinding state leading to uncoordinated spam
- **Thread-local maps everywhere**: Memory leaks, fragmented state, poor cache performance in PathFollow.cpp
- **Reactive pathfinding**: Entities request paths when stuck instead of proactive planning
- **No spatial awareness**: All entities compete equally regardless of player visibility
- **Multiple TTL systems**: Inconsistent cooldown mechanisms scattered throughout codebase
- **Poor scalability**: 100 entities = 85 requests/frame = 25% timeout rate

### Performance Issues
- Memory usage grows with entity count due to per-entity hashmaps
- Performance degrades quadratically as more entities are added
- No coordination between pathfinding requests
- High iteration counts (1500+ for short paths)
- Repeated failures at boundary coordinates

### Specific Code Issues
- **PathFollow.cpp**: Contains thread_local maps that cause memory fragmentation
- **AIManager batch system**: Current rate limiting hack (15 requests/frame) doesn't address root cause
- **Individual TTL values**: 1000ms, 1500ms, 2000ms, 2500ms scattered across different functions
- **No path sharing**: Similar paths computed multiple times
- **Boundary goal failures**: Repeated failures at coordinates like (96,3)

## Proposed Architecture

### 1. Enhanced AIManager (Existing Manager Pattern)
**Follows Engine's Manager singleton pattern with `m_isShutdown` guard**

The pathfinding redesign will be integrated into the existing `AIManager` class to follow the engine's established patterns:

```cpp
// AIManager.hpp additions
class AIManager {
    // ... existing code ...

public:
    // Enhanced pathfinding interface (replaces current batch system)
    void requestPath(EntityID entity, Vector2D start, Vector2D goal, PathPriority priority);
    bool hasPath(EntityID entity) const;
    std::vector<Vector2D> getPath(EntityID entity) const;

private:
    // Pathfinding subsystem components
    std::unique_ptr<PathfindingScheduler> m_pathScheduler;
    std::unique_ptr<PathCache> m_pathCache;
    std::unique_ptr<SpatialPriority> m_spatialPriority;
    
    // Enhanced update method
    void updatePathfinding(float deltaTime, const Vector2D& playerPos);
};

// Internal pathfinding scheduler (not exposed publicly)
class PathfindingScheduler {
public:
    void requestPath(EntityID entity, Vector2D start, Vector2D goal, PathPriority priority);
    void update(float deltaTime, const Vector2D& playerPos, PathfindingGrid* grid);
    bool hasPath(EntityID entity) const;
    std::vector<Vector2D> getPath(EntityID entity) const;

private:
    // Priority queue with spatial sorting
    std::priority_queue<PathRequest> m_requestQueue;
    
    // Results cache  
    std::unordered_map<EntityID, PathResult> m_pathCache;
    
    // Request rate limiting
    static constexpr size_t MAX_REQUESTS_PER_FRAME = 8;
    
    // CollisionManager integration for dynamic priority adjustment  
    int getAreaCongestion(const Vector2D& center, float radius);
    bool hasRealtimeObstacles(const Vector2D& start, const Vector2D& goal);
    
    // ThreadSystem integration for background processing
    void processRequestsBatch(std::vector<PathRequest> batch);
};
```

### 2. Spatial Priority System
**Zones based on distance from player:**
- **Near (0-800px)**: High priority, every frame updates
- **Medium (800-1600px)**: Normal priority, every 2-3 frames
- **Far (1600-3200px)**: Low priority, every 5-10 frames  
- **Culled (3200px+)**: No pathfinding, simple movement patterns

### 3. Integrated Crowd Management
**Keep existing Crowd.hpp/cpp - it's already well-designed!**

The existing `AIInternal::Crowd` system provides essential functionality that complements pathfinding:

```cpp
// KEEP EXISTING: src/ai/internal/Crowd.hpp/cpp
namespace AIInternal {
    // Already provides:
    Vector2D ApplySeparation(...);           // Anti-clumping for smooth movement
    Vector2D SmoothVelocityTransition(...);  // Reduces visual jitter 
    int CountNearbyEntities(...);            // Crowd density analysis
    int GetNearbyEntitiesWithPositions(...); // Spatial awareness
    
    // Separation parameters for different behavior types:
    SeparationParams::COMBAT_*, MOVEMENT_*, IDLE_*, FLEE_*
}
```

**CollisionManager Integration Points:**
The existing system already uses CollisionManager effectively:

```cpp
// EXISTING: Crowd.cpp uses CollisionManager for spatial queries
auto &cm = CollisionManager::Instance();
cm.queryArea(area, queryResults);                    // Fast spatial lookups
cm.getBodyCenter(id, entityPos);                     // Entity position queries
cm.isDynamic(id) || cm.isKinematic(id);             // Body type filtering
!cm.isTrigger(id);                                   // Exclude triggers
```

**Integration with new pathfinding:**
- PathfindingScheduler will use `cm.queryArea()` for density-aware prioritization
- PathCache will use `cm.queryArea()` to detect congested areas for cache eviction
- SpatialPriority zones will use existing CollisionManager spatial hash for efficiency
- Dynamic obstacle detection can use `cm.queryArea()` for real-time path invalidation
- Enhanced NavigationResult will include crowd-aware velocity suggestions

### 4. Path Sharing System  
**New internal class for path caching and reuse**

```cpp
// src/ai/internal/PathCache.hpp (new file)
namespace AIInternal {
class PathCache {
public:
    // Check if similar path exists within tolerance
    std::optional<std::vector<Vector2D>> findSimilarPath(Vector2D start, Vector2D goal, float tolerance = 64.0f);
    
    // Cache completed paths for reuse  
    void cachePath(Vector2D start, Vector2D goal, const std::vector<Vector2D>& path);
    
    // Integrate with crowd system for cache eviction
    void evictPathsInCrowdedAreas(const Vector2D& playerPos);
    
    // Engine-style cleanup
    void shutdown();
    
private:
    // Use existing CollisionManager spatial hash instead of custom implementation
    struct PathSegment { Vector2D start, end; std::vector<Vector2D> waypoints; };
    std::unordered_map<uint64_t, PathSegment> m_cachedPaths;
    
    // LRU eviction (simple approach)
    std::queue<uint64_t> m_evictionQueue;
    static constexpr size_t MAX_CACHED_PATHS = 256;
    
    uint64_t hashPath(Vector2D start, Vector2D goal) const;
};
} // namespace AIInternal
```

### 5. Hierarchical Pathfinding
**For long distances (>1200px):**
- Use waypoint-based navigation
- Pre-compute major navigation routes
- Local pathfinding only for final approach

**For short distances (<400px):**
- Direct line-of-sight checks
- Simple obstacle avoidance
- No A* unless necessary

### 6. Enhanced AIBehavior Integration
**Work within existing AI behavior framework**

Instead of creating a separate NavigationComponent, enhance the existing AIBehavior system:

```cpp
// Enhanced AIBehavior base class (existing file)
class AIBehavior {
    // ... existing methods ...
    
protected:
    // Enhanced navigation helpers (replace PathFollow.cpp functions)
    bool requestNavigation(EntityPtr entity, Vector2D goal, PathPriority priority = PathPriority::Normal);
    NavigationResult updateNavigation(EntityPtr entity, Vector2D currentPos, float speed);
    bool hasValidPath(EntityPtr entity) const;
    
private:
    // No per-behavior state - all managed by AIManager
};

// Navigation result structure with crowd integration
namespace AIInternal {
struct NavigationResult {
    enum class Status { NoPath, Following, Reached, Blocked };
    Status status = Status::NoPath;
    Vector2D nextWaypoint;
    Vector2D suggestedVelocity;
    
    // Crowd management integration
    Vector2D crowdAdjustedVelocity;  // Velocity after ApplySeparation
    int nearbyEntityCount = 0;       // From CountNearbyEntities
    bool shouldYield = false;
    float yieldDuration = 0.0f;
};
} // namespace AIInternal
```

## Implementation Plan

### Current System Context
The engine uses a Manager singleton pattern with:
- `AIManager::Instance()` - Main AI coordinator  
- Existing batch processing: `m_pathBatchBuffer`, `processBatchedPathfinding()`
- ThreadSystem integration with `TaskPriority::High` for pathfinding
- Current public interface: `requestPathAsync()`, `hasAsyncPath()`, `getAsyncPath()`

### Phase 1: Foundation Integration ✅ COMPLETED
**Goal: Replace current hack fixes with proper architecture**

- [x] ✅ Create `src/ai/internal/PathfindingScheduler.hpp/cpp` 
- [x] ✅ Add `std::unique_ptr<PathfindingScheduler> m_pathScheduler;` to AIManager private members
- [x] ✅ Replace current `processBatchedPathfinding()` rate limiting with proper scheduling
- [x] ✅ Implement priority queue: `std::priority_queue<PathRequest>` with spatial comparator
- [x] ✅ Use existing ThreadSystem patterns: `HammerEngine::ThreadSystem::Instance().enqueueTask()`
- [x] ✅ Maintain backward compatibility: existing `requestPathAsync()` calls PathfindingScheduler
- [x] ✅ Integrate PathCache for path reuse and performance optimization

**Status**: Phase 1 is COMPLETE. PathfindingScheduler is fully integrated into AIManager with:
- Priority-based request queuing with spatial sorting
- PathCache integration for path reuse (64px tolerance)
- CollisionManager integration for area congestion detection  
- Proper cleanup and shutdown handling
- Thread-safe request/result management
- Performance statistics and logging

### Phase 2: Enhanced Pathfinding 🔄 IN PROGRESS  
**Goal: Complete spatial priority system and hierarchical pathfinding**

- [x] ✅ Implement PathCache with CollisionManager integration for congestion detection
- [x] ✅ Add dynamic obstacle detection using cm.queryArea() for real-time path invalidation
- [x] ✅ Integrate with existing PathfindingGrid class
- [ ] 🔄 **CURRENT TASK**: Add SpatialPriority class using CollisionManager spatial queries
- [ ] Enhanced A* with hierarchical waypoints for long distances  
- [ ] Predictive pathfinding for high-priority entities

**Status**: Phase 2 is PARTIALLY COMPLETE. PathCache is fully implemented with:
- Smart path similarity detection (64px tolerance)  
- LRU cache eviction (256 path limit)
- Congestion-aware cache eviction using CollisionManager queries
- Statistics tracking (hit rates, evictions, etc.)
- Spatial hash-based path quantization for clustering

**REMAINING WORK**: 
- Create dedicated `SpatialPriority` class to formalize distance-based priority zones
- Currently spatial priority is handled inline in PathfindingScheduler::adjustPriorityByDistance()
- Need hierarchical pathfinding for long-distance navigation (>1200px)

### Phase 3: Behavior Migration ⚠️ CRITICAL PRIORITY
**Goal: Eliminate thread_local memory leaks and uncoordinated entity pathfinding**

- [ ] 🔴 **CRITICAL**: Remove thread_local maps causing memory leaks in PathFollow.cpp
  - Line 40: `static thread_local WorldBoundsCache g_boundsCache;`
  - Line 122: `static thread_local std::unordered_map<EntityID, Uint64> lastDetourAttempt;`
  - Line 325: `static thread_local std::unordered_map<EntityID, std::pair<Uint64, uint8_t>> detourTracking;`
- [ ] Add navigation helpers to AIBehavior base class to replace PathFollow functions
- [ ] Migrate behaviors one-by-one from PathFollow.cpp functions to use PathfindingScheduler directly
- [ ] Centralize all pathfinding state in AIManager instead of per-thread storage
- [ ] Remove PathFollow.cpp after full migration

**Status**: URGENT - Thread-local maps are causing memory fragmentation and preventing scalability.
Current PathFollow.cpp still routes through the old requestPathAsync() pathway, which works but
bypasses many of the optimizations in PathfindingScheduler. The thread_local state prevents 
proper coordination between entities.

### Phase 4: Performance Optimization (Day 15-21)
- [ ] Add performance counters using existing stats system
- [ ] Tune spatial priority zones based on metrics
- [ ] Implement predictive pathfinding for high-priority entities
- [ ] Add graceful degradation under high load

## Success Metrics

### Performance Targets
- **Request Rate**: <15 requests/frame for 100 entities (currently 85)
- **Timeout Rate**: <5% (currently 25%)
- **Iteration Count**: <500 avg for short paths (currently 1500+)
- **Memory Usage**: O(log N) growth instead of O(N²)

### Scalability Targets
- Support 500+ entities with <10% performance degradation
- Consistent frame times regardless of entity pathfinding load
- Graceful degradation when system is overloaded

## Risk Mitigation

### Backwards Compatibility
- Keep old system running in parallel during transition
- Feature flag to switch between old/new systems
- Gradual migration of AI behaviors

### Performance Validation
- Continuous benchmarking during development
- A/B testing between old and new systems
- Rollback plan if performance degrades

## File Structure (Following Engine Patterns)

```
src/ai/internal/                    # Internal implementation details
├── Crowd.hpp/cpp                   # KEEP EXISTING - crowd management
├── PathfindingScheduler.hpp/cpp    # Internal scheduler class (NEW)
├── PathCache.hpp/cpp               # Path sharing system (NEW)
└── SpatialPriority.hpp/cpp         # Distance-based zones (NEW)

src/ai/pathfinding/                 # Existing pathfinding code
├── PathfindingGrid.hpp/cpp         # Enhanced A* (existing)
└── PathfindingTypes.hpp            # Common types/enums (new)

src/managers/                       # Manager classes (existing)
└── AIManager.hpp/cpp               # Enhanced with pathfinding redesign

include/managers/                   # Public headers (existing)  
└── AIManager.hpp                   # Public interface unchanged

include/ai/                         # AI system headers
├── AIBehavior.hpp                  # Enhanced with navigation helpers
└── pathfinding/                    # Pathfinding public headers
    └── PathfindingTypes.hpp        # Common types/enums
```

**Key Changes:**
- PathfindingScheduler is private to AIManager (no public header)
- PathCache and SpatialPriority are internal implementation details
- AIBehavior enhanced with navigation helpers (replaces NavigationComponent)
- Follow existing module organization and naming conventions
- **CollisionManager integration**: Reuse existing spatial hash for all spatial queries
- **Crowd.hpp/cpp preserved**: No changes needed, already well-integrated

---

## Current Implementation Analysis (January 2025)

### ✅ COMPLETED COMPONENTS

#### PathfindingScheduler (Phase 1) 
- **Location**: `src/ai/internal/PathfindingScheduler.hpp/cpp` (553 lines)
- **Integration**: Fully integrated into AIManager with proper singleton pattern
- **Features**:
  - Priority queue with spatial sorting for cache efficiency
  - Distance-based priority adjustment (Near: 0-800px, Medium: 800-1600px, Far: 1600-3200px, Culled: 3200px+)
  - Request rate limiting (MAX_REQUESTS_PER_FRAME = 8)
  - Thread-safe request extraction and result storage
  - Performance statistics tracking
  - Proper shutdown handling with m_isShutdown guard

#### PathCache (Phase 2 Partial)
- **Location**: `src/ai/internal/PathCache.hpp/cpp` (377 lines)
- **Features**:
  - Smart path similarity detection with 64px tolerance
  - LRU cache eviction (MAX_CACHED_PATHS = 256)
  - Congestion-aware cache eviction using CollisionManager
  - Spatial quantization for better path clustering
  - Statistics tracking (hit rate, evictions, total queries)
  - CollisionManager integration for dynamic obstacle detection

### 🔄 PARTIALLY COMPLETED

#### Spatial Priority System
- **Current**: Basic distance-based priority in PathfindingScheduler::adjustPriorityByDistance()
- **Missing**: Dedicated SpatialPriority class for more sophisticated zone management
- **Needed**: Formal priority zone system with dynamic adjustments based on player movement

### 🔴 CRITICAL ISSUES REMAINING

#### Thread-Local Memory Leaks in PathFollow.cpp
- **Line 40**: `static thread_local WorldBoundsCache g_boundsCache;` - Memory leak per thread
- **Line 122**: `static thread_local std::unordered_map<EntityID, Uint64> lastDetourAttempt;` - Grows indefinitely  
- **Line 325**: `static thread_local std::unordered_map<EntityID, std::pair<Uint64, uint8_t>> detourTracking;` - Per-thread entity state

#### Bypassed Optimizations
- PathFollow.cpp still uses old `AIManager::requestPathAsync()` pathway
- Misses PathCache benefits and spatial priority optimizations
- Uncoordinated pathfinding spam (estimated 85 requests/frame for 100 entities)

### 📊 PERFORMANCE STATUS
- **Pathfinding Tests**: All 15 test cases passing ✅
- **Performance**: 50 requests in 33 microseconds (0 μs per request)
- **Success Rate**: 100% for simple paths, 2 waypoints average
- **Memory Usage**: Tests passing for 200 operations

### 🎯 IMMEDIATE PRIORITIES
1. **CRITICAL**: Fix thread_local memory leaks in PathFollow.cpp
2. **HIGH**: Create formal SpatialPriority class  
3. **MEDIUM**: Add hierarchical pathfinding for long distances
4. **LOW**: Fine-tune performance metrics and logging

## Migration Strategy

1. **Parallel Implementation**: Build new system alongside old one
2. **Behavior-by-Behavior**: Migrate one AI behavior at a time
3. **Performance Gates**: Each phase must meet performance targets
4. **Feature Parity**: New system must match old system capabilities
5. **Clean Removal**: Delete old code only after full validation

---

## Implementation Reference

### Key File Locations
```
src/managers/AIManager.cpp:1960          # Current processBatchedPathfinding()
src/ai/internal/PathFollow.cpp:122       # TTL values to be replaced
src/ai/internal/Crowd.cpp:31            # cm.queryArea() usage example
include/managers/AIManager.hpp:570       # Current batch member variables
```

### Engine Patterns to Follow
```cpp
// Manager singleton pattern
class AIManager {
    static AIManager& Instance() { static AIManager instance; return instance; }
    std::atomic<bool> m_isShutdown{false};
};

// ThreadSystem usage
HammerEngine::ThreadSystem::Instance().enqueueTask(
    [this, batch = std::move(batchToProcess)]() mutable { 
        this->processPathBatch(batch); 
    },
    HammerEngine::TaskPriority::High
);

// CollisionManager spatial queries
auto &cm = CollisionManager::Instance();
cm.queryArea(area, queryResults);
cm.getBodyCenter(id, entityPos);
```

### Critical Constraints
- **No breaking changes** to AIManager public interface
- **Preserve thread safety** of existing batch system
- **Reuse existing spatial infrastructure** (CollisionManager)
- **Follow engine coding standards** (m_ prefix, 4-space indent, RAII)

---

## Notes for Claude Code

When working on this redesign:

1. **Start with Phase 1** - Don't jump ahead to complex features
2. **Follow engine patterns** - Use existing Manager singleton, ThreadSystem, CollisionManager
3. **Maintain compatibility** - Keep existing public interface working
4. **Measure performance** - Compare before/after request rates and timeout percentages
5. **Test incrementally** - Each component should be testable in isolation

**Current Problem**: 85 requests/frame, 25% timeout rate, thread_local memory leaks
**Target**: <15 requests/frame, <5% timeout rate, centralized state management

Current focus: **Phase 1 - Foundation Integration**
Next milestone: PathfindingScheduler replacing current batch rate limiting