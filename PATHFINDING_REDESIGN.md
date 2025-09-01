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

### Phase 1: Foundation Integration (Day 1-3)
**Goal: Replace current hack fixes with proper architecture**

- [ ] Create `src/ai/internal/PathfindingScheduler.hpp/cpp` 
- [ ] Add `std::unique_ptr<PathfindingScheduler> m_pathScheduler;` to AIManager private members
- [ ] Replace current `processBatchedPathfinding()` rate limiting with proper scheduling
- [ ] Implement priority queue: `std::priority_queue<PathRequest>` with spatial comparator
- [ ] Use existing ThreadSystem patterns: `HammerEngine::ThreadSystem::Instance().enqueueTask()`
- [ ] Maintain backward compatibility: existing `requestPathAsync()` calls PathfindingScheduler

### Phase 2: Enhanced Pathfinding (Day 4-7)
- [ ] Add SpatialPriority class using CollisionManager spatial queries
- [ ] Implement PathCache with CollisionManager integration for congestion detection
- [ ] Enhanced A* with hierarchical waypoints for long distances
- [ ] Add dynamic obstacle detection using cm.queryArea() for real-time path invalidation
- [ ] Integrate with existing PathfindingGrid class

### Phase 3: Behavior Migration (Day 8-14)
- [ ] Add navigation helpers to AIBehavior base class
- [ ] Migrate behaviors one-by-one from PathFollow.cpp functions
- [ ] Replace thread_local maps with AIManager-managed state
- [ ] Remove PathFollow.cpp after full migration

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