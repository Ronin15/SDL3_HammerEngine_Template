# Detailed Implementation Plan: Physics & Pathfinding (Repo-Aligned)

## Overview
This plan aligns Physics (AABB collisions) and Pathfinding with the current SDL3 HammerEngine Template codebase. It follows existing patterns: singleton managers under `include/managers`, implementation in `src/<module>`, `Vector2D` for math, `HammerEngine::ThreadSystem` for parallelism, and World-driven obstacle data via `WorldManager` and world events.

Key repo realities reflected here:
- Use `Vector2D` from `include/utils/Vector2D.hpp` (do NOT introduce a new vector type).
- Managers are singletons with `::Instance()` and shutdown guards.
- GameEngine updates global systems, then delegates to `GameStateManager::update()`.
- World obstacles exist in `HammerEngine::Tile` (`ObstacleType`, `isWater`).
- Threading goes through `HammerEngine::ThreadSystem` + `WorkerBudget`.

## Phase 0: Groundwork (types, order, integration hooks)
- Math: adopt `Vector2D` everywhere; headers avoid SDL includes at API boundaries.
- Manager shape: `PhysicsManager` as a singleton under `include/managers/`, mirroring others.
- Update order: AI (sets velocities) → Physics (integrates, resolves) → Events/Particles → Game states render/update.
- World coupling: derive static colliders and pathfinding grid from `WorldManager` grid and listen to `WorldLoadedEvent`, `TileChangedEvent`, `WorldUnloadedEvent`.
- Threading: use `HammerEngine::ThreadSystem::Instance()` and `TaskPriority::High` for physics broadphase; `TaskPriority::Normal/High` for pathfinding batches.
- Tile size: default `32.0f` cell size; compute from world dimensions if needed.

## Phase 1: Physics Integration (AABB + Spatial Hash)

### 1.1 Files to Create
```
include/physics/
├── AABB.hpp
├── CollisionBody.hpp
├── CollisionInfo.hpp
└── SpatialHash.hpp

include/managers/
└── PhysicsManager.hpp

src/physics/
├── AABB.cpp
├── SpatialHash.cpp
└── CollisionBody.cpp   // only if non-trivial helpers are needed

src/managers/
└── PhysicsManager.cpp
```

### 1.2 `AABB.hpp` (header-only API, logic in `.cpp`)
```cpp
#ifndef AABB_HPP
#define AABB_HPP
#include "utils/Vector2D.hpp"

namespace HammerEngine {

struct AABB {
    Vector2D center;   // world center
    Vector2D halfSize; // half extents (w/2, h/2)

    AABB() = default;
    AABB(float cx, float cy, float hw, float hh) : center(cx, cy), halfSize(hw, hh) {}

    float left() const { return center.getX() - halfSize.getX(); }
    float right() const { return center.getX() + halfSize.getX(); }
    float top() const { return center.getY() - halfSize.getY(); }
    float bottom() const { return center.getY() + halfSize.getY(); }

    bool intersects(const AABB& other) const;        // implemented in AABB.cpp
    bool contains(const Vector2D& p) const;          // implemented in AABB.cpp
    Vector2D closestPoint(const Vector2D& p) const;  // implemented in AABB.cpp
};

} // namespace HammerEngine

#endif // AABB_HPP
```

### 1.3 `CollisionBody.hpp`
```cpp
#ifndef COLLISION_BODY_HPP
#define COLLISION_BODY_HPP
#include <cstdint>
#include "physics/AABB.hpp"
#include "entities/Entity.hpp" // for EntityID alias

namespace HammerEngine {

enum class BodyType : uint8_t { STATIC, KINEMATIC, DYNAMIC };

// Bitmask collision layers (combine via bitwise OR)
enum CollisionLayer : uint32_t {
    Layer_Default     = 1u << 0,
    Layer_Player      = 1u << 1,
    Layer_Enemy       = 1u << 2,
    Layer_Environment = 1u << 3,
    Layer_Projectile  = 1u << 4,
    Layer_Trigger     = 1u << 5,
};

struct CollisionBody {
    EntityID id{0};
    AABB aabb{};
    Vector2D velocity{0,0};
    Vector2D acceleration{0,0};
    BodyType type{BodyType::DYNAMIC};
    uint32_t layer{Layer_Default};
    uint32_t collidesWith{0xFFFFFFFFu};
    bool enabled{true};
    bool isTrigger{false};
    float mass{1.0f};
    float friction{0.8f};
    float restitution{0.0f};

    bool shouldCollideWith(const CollisionBody& other) const {
        return enabled && other.enabled && (collidesWith & other.layer) != 0u;
    }
};

} // namespace HammerEngine

#endif // COLLISION_BODY_HPP
```

### 1.4 `CollisionInfo.hpp`
```cpp
#ifndef COLLISION_INFO_HPP
#define COLLISION_INFO_HPP
#include "physics/AABB.hpp"

namespace HammerEngine {

struct CollisionInfo {
    EntityID a{0};
    EntityID b{0};
    Vector2D normal{0,0};
    float penetration{0.0f};
    bool trigger{false};
};

} // namespace HammerEngine

#endif // COLLISION_INFO_HPP
```

### 1.5 `SpatialHash.hpp/.cpp`
- Fixed cell size (default: `32.0f`) and 2D hash (unordered_map keyed by cell coords).
- API: `insert(EntityID, const AABB&)`, `remove(EntityID)`, `update(EntityID, const AABB&)`, `query(const AABB&, out vector<EntityID>&)`.
- Keep header minimal; implement hashing and storage in `.cpp`.

### 1.6 `PhysicsManager.hpp/.cpp` (singleton)
Header (singleton, minimal includes, forward declare where possible):
```cpp
#ifndef PHYSICS_MANAGER_HPP
#define PHYSICS_MANAGER_HPP
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstddef>

namespace HammerEngine {
struct AABB; struct CollisionBody; struct CollisionInfo;

class PhysicsManager {
public:
    static PhysicsManager& Instance();
    bool init();
    void clean();
    bool isInitialized() const { return m_initialized; }
    bool isShutdown() const { return m_isShutdown; }

    // Tick
    void update(float dt);

    // Bodies
    void addBody(EntityID id, const AABB& aabb, BodyType type);
    void removeBody(EntityID id);
    void setBodyEnabled(EntityID id, bool enabled);
    void setBodyLayer(EntityID id, uint32_t layerMask, uint32_t collideMask);
    void setKinematicPose(EntityID id, const Vector2D& center);
    void setVelocity(EntityID id, const Vector2D& v);

    // Queries
    bool overlaps(EntityID a, EntityID b) const;
    std::vector<CollisionInfo> queryArea(const AABB& area) const;

    // World coupling
    void rebuildStaticFromWorld();                // build colliders from WorldManager grid
    void onTileChanged(int x, int y);             // update a specific cell
    void setWorldBounds(float minX, float minY, float maxX, float maxY);

    // Callbacks
    using CollisionCB = std::function<void(const CollisionInfo&)>;
    void addCollisionCallback(CollisionCB cb);

    // Metrics
    size_t getBodyCount() const { return m_bodies.size(); }

private:
    PhysicsManager() = default;
    ~PhysicsManager() { if (!m_isShutdown) clean(); }
    PhysicsManager(const PhysicsManager&) = delete;
    PhysicsManager& operator=(const PhysicsManager&) = delete;

    void integrate(float dt);
    void broadphase(std::vector<std::pair<EntityID,EntityID>>& pairs);
    void narrowphase(const std::vector<std::pair<EntityID,EntityID>>& pairs,
                     std::vector<CollisionInfo>& collisions);
    void resolve(const CollisionInfo& info);
    void subscribeWorldEvents(); // hook EventManager via WorldManager

    bool m_initialized{false};
    bool m_isShutdown{false};
    Vector2D m_gravity{0.0f, 0.0f}; // start without gravity for top-down

    // storage
    std::unordered_map<EntityID, std::shared_ptr<CollisionBody>> m_bodies;
    std::vector<CollisionCB> m_callbacks;
};

} // namespace HammerEngine

#endif // PHYSICS_MANAGER_HPP
```

Implementation highlights (`PhysicsManager.cpp`):
- Use `HammerEngine::ThreadSystem::Instance()` with `TaskPriority::High` to parallelize broadphase when `m_bodies.size() > 250`.
- Integrate dynamic bodies: `v += a*dt`, apply friction, clamp small velocities, then propose next pose; resolve against statics and dynamics.
- World coupling: `rebuildStaticFromWorld()` iterates `WorldManager::Instance().getWorldData()->grid` and adds STATIC bodies for `ObstacleType != NONE` and `isWater == true` tiles.
- Subscribe to `WorldLoadedEvent`, `TileChangedEvent`, `WorldUnloadedEvent` via `WorldManager::setupEventHandlers()` pattern (store handler tokens locally to cleanly unregister in `clean()`).

### 1.7 GameEngine integration
- Files to modify:
  - `include/core/GameEngine.hpp`: forward declare `class PhysicsManager;` and add cached pointer `PhysicsManager* mp_physicsManager{nullptr};` alongside other manager caches.
  - `src/core/GameEngine.cpp`:
    - In initialization task list: enqueue a task to `PhysicsManager::Instance().init()` and on success cache `mp_physicsManager` after validation like other managers.
    - In `update(float deltaTime)`: call `mp_physicsManager->update(deltaTime)` right after AI update and before Event/Particle updates.

Notes:
- Do not add extra synchronization beyond the existing GameEngine mutex and double buffer; physics runs inside `GameEngine::update()` like other managers.
- Keep gravity zero by default (top-down). Expose `setGravity()` later if needed.

### 1.8 Entity registration (minimal, non-invasive)
- NPC/Player integration (surgical changes):
  - On spawn/construct (after texture dimensions known), register a DYNAMIC body sized to sprite frame: center at `m_position`, halfSize = `{m_frameWidth/2, m_height/2}`.
  - Each NPC `update`: after computing velocity, also call `PhysicsManager::Instance().setVelocity(getID(), m_velocity);` and then set `m_position` from the physics body’s resolved pose at the end of physics. To avoid double-integrating, stop applying manual world bounds bounces when physics is enabled.
- Alternatively (phase 1.0), keep current NPC self-integration and only use physics for overlap queries and static collision checks. Full migration to physics-driven movement can be done in a follow-up patch.

## Phase 2: Pathfinding Integration (A*)

### 2.1 Files to Create
```
include/ai/pathfinding/
├── PathfindingGrid.hpp
├── PathfindingRequest.hpp
└── PathSmoother.hpp

src/ai/pathfinding/
├── PathfindingGrid.cpp
└── PathSmoother.cpp
```

### 2.2 `PathfindingGrid` (tile-aware grid)
```cpp
#ifndef PATHFINDING_GRID_HPP
#define PATHFINDING_GRID_HPP
#include <vector>
#include <utility>
#include "utils/Vector2D.hpp"

namespace HammerEngine {

enum class PathfindingResult { SUCCESS, NO_PATH_FOUND, INVALID_START, INVALID_GOAL, TIMEOUT };

class PathfindingGrid {
public:
    PathfindingGrid(int width, int height, float cellSize, const Vector2D& worldOffset);

    void rebuildFromWorld();                 // pull from WorldManager::grid
    PathfindingResult findPath(const Vector2D& start, const Vector2D& goal,
                               std::vector<Vector2D>& outPath);

    void setAllowDiagonal(bool allow);
    void setMaxIterations(int maxIters);
    void setCosts(float straight, float diagonal);

private:
    int m_w, m_h; float m_cell; Vector2D m_offset;
    std::vector<uint8_t> m_blocked; // 0 walkable, 1 blocked
    std::vector<float> m_weight;    // movement multipliers per cell

    bool isBlocked(int gx, int gy) const;
    bool inBounds(int gx, int gy) const;
    std::pair<int,int> worldToGrid(const Vector2D& w) const;
    Vector2D gridToWorld(int gx, int gy) const;
};

} // namespace HammerEngine

#endif // PATHFINDING_GRID_HPP
```

Implementation highlights (`PathfindingGrid.cpp`):
- Rebuild obstacles from `WorldManager::Instance().getWorldData()->grid`: mark blocked when `ObstacleType != NONE` or `isWater`.
- Cell size defaults to 32.0f; offset typically `{0,0}`; compute world/grid transforms accordingly.
- Standard A* with open/closed sets; allow diagonals toggle; guard `maxIterations` to prevent stalls.

### 2.3 `PathfindingRequest.hpp`
```cpp
#ifndef PATHFINDING_REQUEST_HPP
#define PATHFINDING_REQUEST_HPP
#include <vector>
#include <functional>
#include <cstdint>
#include "utils/Vector2D.hpp"
namespace HammerEngine {

enum class RequestPriority : uint8_t { LOW, NORMAL, HIGH, CRITICAL };
enum class RequestStatus : uint8_t { PENDING, PROCESSING, COMPLETED, FAILED, CANCELLED };

struct PathfindingRequest {
    uint32_t requestId{0};
    EntityID entityId{0};
    Vector2D start; Vector2D goal;
    RequestPriority priority{RequestPriority::NORMAL};
    RequestStatus status{RequestStatus::PENDING};
    std::vector<Vector2D> path;
    PathfindingResult result{PathfindingResult::SUCCESS};
    std::function<void(const PathfindingRequest&)> onComplete; // optional
};

} // namespace HammerEngine

#endif // PATHFINDING_REQUEST_HPP
```

### 2.4 AIManager integration (request queue + caching)
- Header: `include/managers/AIManager.hpp`
  - Add forward declarations only: `namespace HammerEngine { class PathfindingGrid; struct PathfindingRequest; }` and include headers in `.cpp` to keep API surface minimal.
  - Private members:
    - `std::unique_ptr<HammerEngine::PathfindingGrid> m_pathGrid;`
    - `std::vector<HammerEngine::PathfindingRequest> m_pathQueue;`
    - `std::unordered_map<EntityID, std::vector<Vector2D>> m_entityPaths;`
    - `uint32_t m_nextPathReqId{1};`
  - Public API:
    - `uint32_t requestPath(EntityPtr entity, const Vector2D& start, const Vector2D& goal, RequestPriority prio);`
    - `bool hasPath(EntityPtr entity) const;`
    - `std::vector<Vector2D> getPath(EntityPtr entity) const;`
    - `void clearPath(EntityPtr entity);`
    - `void initializePathGridFromWorld();`

- Implementation: `src/managers/AIManager.cpp`
  - In `init()`: defer grid creation until a world exists. Add `initializePathGridFromWorld()` that queries `WorldManager` dimensions and builds `m_pathGrid` with `(width,height,32.0f)`.
  - In `update(float dt)`: periodically process pending requests:
    - Batch submit to `ThreadSystem` with `TaskPriority::Normal/High` depending on priority; collect futures and write results back.
    - Rebuild or update grid on world events (subscribe similarly to PhysicsManager) or when a threshold of `TileChangedEvent`s accrues.
  - Implement the request API with ID generation, deduping by entity where helpful, and result caching in `m_entityPaths`.

### 2.5 Behavior integration (opt-in)
- `PatrolBehavior` and `ChaseBehavior` keep current direct-steering behavior by default.
- Add optional path fetch/follow when a compile-time flag `HAMMER_ENABLE_PATHFINDING` is defined:
  - For `PatrolBehavior::executeLogic`: when waypoint selected, call `AIManager::Instance().requestPath(...)`; if a path exists, follow it; else fall back to current steering logic.
  - For `ChaseBehavior`: re-request at `pathRecalculateInterval` with `RequestPriority::HIGH`.
- Keep behavior headers unchanged where possible; put path code in `.cpp` and guard with `#ifdef HAMMER_ENABLE_PATHFINDING`.

## Phase 3: Engine Wiring & Events

### 3.1 GameEngine updates (exact touch points)
- `include/core/GameEngine.hpp`:
  - Add forward declaration `class PhysicsManager;`
  - Add member `PhysicsManager* mp_physicsManager{nullptr};`

- `src/core/GameEngine.cpp`:
  - In initialization tasks (alongside AI/Event/Particle/World managers), enqueue init:
    ```cpp
    initTasks.push_back(HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult([](){
      auto& pm = HammerEngine::PhysicsManager::Instance();
      if (!pm.init()) { GAMEENGINE_CRITICAL("Failed to initialize PhysicsManager"); return false; }
      return true;
    }));
    ```
  - After tasks complete, cache pointer like others:
    ```cpp
    auto& pmt = HammerEngine::PhysicsManager::Instance();
    if (!pmt.isInitialized()) { GAMEENGINE_CRITICAL("PhysicsManager not initialized before caching!"); return false; }
    mp_physicsManager = &pmt;
    ```
  - In `GameEngine::update(float deltaTime)`, after `mp_aiManager->update(deltaTime)`:
    ```cpp
    if (mp_physicsManager) { mp_physicsManager->update(deltaTime); } else { GAMEENGINE_ERROR("PhysicsManager cache is null!"); }
    ```

### 3.2 Event subscriptions
- PhysicsManager subscribes to world events to rebuild or patch static colliders.
- AIManager subscribes similarly to rebuild the pathfinding grid.
- Use `WorldManager::setupEventHandlers()` pattern and keep handler tokens for clean unregistration in `clean()`.

## Phase 4: Validation & Tests
- Unit tests (Boost.Test):
  - `tests/physics/AABBTests.cpp`: intersect/contain/closestPoint cases.
  - `tests/physics/SpatialHashTests.cpp`: insert/update/query.
  - `tests/ai/PathfindingGridTests.cpp`: simple maps with obstacles, diagonal on/off, timeouts.
- Integration tests:
  - Extend `tests/world/WorldManagerTests.cpp` to assert `initializePathGridFromWorld()` dimensions and blocked counts match `ObstacleType` distribution.
- Scripts: add runners under `tests/test_scripts/` consistent with existing patterns.

## Phase 5: Performance & Threading
- Physics:
  - Switch to threaded broadphase when `m_bodies.size() > 250` using `TaskPriority::High`.
  - Track `collisionChecksThisFrame` and `activeCollisionsThisFrame`; expose getters for profiling.
- Pathfinding:
  - Batch-submit multiple requests per frame; cap max iterations to avoid long stalls.
  - Cache last successful path per entity and allow partial-path option for responsiveness.
- Budgeting: adopt `WorkerBudget` to avoid starving other subsystems.

## Open Questions / Assumptions
- Gravity is off for top-down; enable via API later if needed.
- NPC/Player sizes taken from sprite frame size; if hitboxes differ, add per-entity physics payload in a future pass.
- World tile size assumed 32.0f; if this changes, compute `cellSize` from world bounds and grid dims.

## Implementation Checklist (AI-friendly)
- Create headers/impl files exactly as listed (paths above).
- Implement `AABB.cpp`, `SpatialHash.cpp`, `PhysicsManager.cpp`, `PathfindingGrid.cpp`, `PathSmoother.cpp` minimal viable features as described.
- Wire PhysicsManager into GameEngine init/update; cache pointer.
- Add world event subscriptions for Physics and AI pathfinding grid.
- Add optional behavior integration under `HAMMER_ENABLE_PATHFINDING` flag.
- Add tests and scripts; run `./run_all_tests.sh --core-only --errors-only`.
- Verify in debug run that AI updates still run, physics update called, and no deadlocks occur.

## Notes on Code Style
- Keep headers minimal (forward declare; include in `.cpp`).
- Use RAII and smart pointers; no raw `new/delete`.
- Prefer STL algorithms; avoid ad-hoc loops where possible.
- Follow naming and brace conventions used by existing managers.
