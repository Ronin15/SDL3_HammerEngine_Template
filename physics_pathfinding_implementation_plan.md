# Detailed Implementation Plan: Physics & Pathfinding Integration

## Overview
This plan integrates AABB collision physics and pathfinding into the SDL3 HammerEngine Template, maintaining the existing architecture patterns and performance characteristics.

## Phase 1: Physics Integration (Week 1-2)

### Step 1.1: Core Physics Structures

#### Files to Create:
```
include/physics/
├── AABB.hpp
├── CollisionBody.hpp
├── CollisionInfo.hpp
└── SpatialHash.hpp

src/physics/
├── AABB.cpp
├── CollisionBody.cpp
└── SpatialHash.cpp
```

#### AABB.hpp Implementation:
```cpp
#pragma once
#include <SDL3/SDL.h>

struct Vector2 {
    float x, y;
    Vector2() : x(0), y(0) {}
    Vector2(float x, float y) : x(x), y(y) {}
    
    Vector2 operator+(const Vector2& other) const { return {x + other.x, y + other.y}; }
    Vector2 operator-(const Vector2& other) const { return {x - other.x, y - other.y}; }
    Vector2 operator*(float scalar) const { return {x * scalar, y * scalar}; }
    float length() const;
    Vector2 normalized() const;
};

struct AABB {
    Vector2 position;  // Center position
    Vector2 size;      // Width and height
    
    AABB() = default;
    AABB(float x, float y, float w, float h) : position(x, y), size(w, h) {}
    
    float left() const { return position.x - size.x * 0.5f; }
    float right() const { return position.x + size.x * 0.5f; }
    float top() const { return position.y - size.y * 0.5f; }
    float bottom() const { return position.y + size.y * 0.5f; }
    
    bool intersects(const AABB& other) const;
    AABB getIntersection(const AABB& other) const;
    bool contains(const Vector2& point) const;
    Vector2 getClosestPoint(const Vector2& point) const;
};
```

#### CollisionBody.hpp:
```cpp
#pragma once
#include "AABB.hpp"
#include <memory>

enum class CollisionLayer : uint8_t {
    DEFAULT = 0,
    PLAYER = 1,
    ENEMY = 2,
    ENVIRONMENT = 4,
    PROJECTILE = 8,
    TRIGGER = 16
};

enum class BodyType : uint8_t {
    STATIC,      // Immovable (walls, terrain)
    KINEMATIC,   // Moves but not affected by physics
    DYNAMIC      // Full physics simulation
};

struct CollisionBody {
    uint32_t entityId;
    AABB bounds;
    Vector2 velocity{0, 0};
    Vector2 acceleration{0, 0};
    
    BodyType bodyType = BodyType::DYNAMIC;
    CollisionLayer layer = CollisionLayer::DEFAULT;
    CollisionLayer collidesWith = static_cast<CollisionLayer>(0xFF); // All layers
    
    bool isEnabled = true;
    bool isTrigger = false;
    float mass = 1.0f;
    float friction = 0.8f;
    float restitution = 0.0f; // Bounciness
    
    // For spatial hashing
    int32_t gridX = -1, gridY = -1;
    
    CollisionBody(uint32_t id, const AABB& aabb) : entityId(id), bounds(aabb) {}
    
    bool shouldCollideWith(const CollisionBody& other) const;
};
```

#### CollisionInfo.hpp:
```cpp
#pragma once
#include "AABB.hpp"

struct CollisionInfo {
    uint32_t entityA;
    uint32_t entityB;
    Vector2 contactPoint;
    Vector2 contactNormal;
    float penetrationDepth;
    bool isTrigger;
    
    CollisionInfo() = default;
    CollisionInfo(uint32_t a, uint32_t b, Vector2 point, Vector2 normal, float depth, bool trigger = false)
        : entityA(a), entityB(b), contactPoint(point), contactNormal(normal), 
          penetrationDepth(depth), isTrigger(trigger) {}
};
```

### Step 1.2: PhysicsManager Core

#### include/managers/PhysicsManager.hpp:
```cpp
#pragma once
#include "../physics/CollisionBody.hpp"
#include "../physics/SpatialHash.hpp"
#include "../physics/CollisionInfo.hpp"
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

class PhysicsManager {
public:
    // Callback types
    using CollisionCallback = std::function<void(const CollisionInfo&)>;
    using TriggerCallback = std::function<void(uint32_t trigger, uint32_t other, bool entering)>;
    
    struct RaycastHit {
        bool hit = false;
        uint32_t entityId = 0;
        Vector2 point;
        Vector2 normal;
        float distance = 0.0f;
    };

private:
    // Core storage
    std::vector<std::shared_ptr<CollisionBody>> bodies;
    std::unordered_map<uint32_t, std::shared_ptr<CollisionBody>> bodyMap;
    
    // Spatial optimization
    std::unique_ptr<SpatialHash> spatialHash;
    
    // Callbacks
    std::vector<CollisionCallback> collisionCallbacks;
    std::vector<TriggerCallback> triggerCallbacks;
    
    // Threading integration
    static constexpr size_t THREADING_THRESHOLD = 100;
    bool useThreading = false;
    
    // Performance tracking
    size_t collisionChecksThisFrame = 0;
    size_t activeCollisionsThisFrame = 0;
    
    // Internal methods
    void updateSpatialHash();
    void resolveCollision(CollisionBody& bodyA, CollisionBody& bodyB, const CollisionInfo& collision);
    std::vector<CollisionInfo> broadPhaseCollision();
    std::vector<CollisionInfo> narrowPhaseCollision(const std::vector<std::pair<uint32_t, uint32_t>>& pairs);
    void integrateVelocity(float deltaTime);
    
public:
    PhysicsManager();
    ~PhysicsManager() = default;
    
    // Main update loop - called from GameEngine
    void update(float deltaTime);
    
    // Body management
    void addCollisionBody(uint32_t entityId, const AABB& bounds, BodyType type = BodyType::DYNAMIC);
    void removeCollisionBody(uint32_t entityId);
    void updateBodyPosition(uint32_t entityId, const Vector2& newPosition);
    void updateBodyVelocity(uint32_t entityId, const Vector2& velocity);
    void setBodyEnabled(uint32_t entityId, bool enabled);
    
    // Collision queries
    bool checkCollision(uint32_t entityA, uint32_t entityB) const;
    std::vector<CollisionInfo> getCollisions(uint32_t entityId) const;
    std::vector<uint32_t> getOverlappingBodies(const AABB& area) const;
    std::vector<uint32_t> getStaticBodies() const; // For pathfinding obstacles
    
    // Raycasting
    RaycastHit raycast(const Vector2& origin, const Vector2& direction, float maxDistance = 1000.0f) const;
    std::vector<RaycastHit> raycastAll(const Vector2& origin, const Vector2& direction, float maxDistance = 1000.0f) const;
    
    // Callbacks
    void addCollisionCallback(const CollisionCallback& callback);
    void addTriggerCallback(const TriggerCallback& callback);
    
    // World settings
    void setGravity(const Vector2& gravity) { this->gravity = gravity; }
    void setWorldBounds(const AABB& bounds) { worldBounds = bounds; }
    
    // Performance info
    size_t getBodyCount() const { return bodies.size(); }
    size_t getCollisionChecks() const { return collisionChecksThisFrame; }
    
private:
    Vector2 gravity{0, 980.0f}; // Default gravity
    AABB worldBounds{0, 0, 10000, 10000}; // World boundaries
};
```

### Step 1.3: GameEngine Integration

#### Modify src/GameEngine.cpp:
```cpp
// Add to includes
#include "managers/PhysicsManager.hpp"

// Add to GameEngine class private members
std::unique_ptr<PhysicsManager> physicsManager;

// In GameEngine constructor (after other managers)
physicsManager = std::make_unique<PhysicsManager>();

// In main game loop update (before rendering)
void GameEngine::update(float deltaTime) {
    // ... existing updates ...
    
    // Update physics after AI but before rendering
    physicsManager->update(deltaTime);
    
    // ... rest of updates ...
}

// Add getter method
PhysicsManager& GameEngine::getPhysicsManager() {
    return *physicsManager;
}
```

#### Modify include/GameEngine.hpp:
```cpp
// Add forward declaration
class PhysicsManager;

// Add to public methods
PhysicsManager& getPhysicsManager();
```

### Step 1.4: Threading Integration

#### Modify src/managers/PhysicsManager.cpp:
```cpp
void PhysicsManager::update(float deltaTime) {
    collisionChecksThisFrame = 0;
    activeCollisionsThisFrame = 0;
    
    // Integrate velocity for dynamic bodies
    integrateVelocity(deltaTime);
    
    // Update spatial hash
    updateSpatialHash();
    
    // Collision detection - use threading for large body counts
    std::vector<CollisionInfo> collisions;
    
    if (useThreading && bodies.size() > THREADING_THRESHOLD) {
        // Use ThreadSystem for collision detection
        auto& threadSystem = GameEngine::getInstance().getThreadSystem();
        
        // Submit collision detection tasks with HIGH priority
        auto broadPhaseFuture = threadSystem.submit(Priority::HIGH, [this]() {
            return broadPhaseCollision();
        });
        
        auto broadPhasePairs = broadPhaseFuture.get();
        
        // Narrow phase collision detection
        collisions = narrowPhaseCollision(broadPhasePairs);
    } else {
        // Single-threaded collision detection
        auto broadPhasePairs = broadPhaseCollision();
        collisions = narrowPhaseCollision(broadPhasePairs);
    }
    
    // Process collisions and trigger callbacks
    for (const auto& collision : collisions) {
        if (collision.isTrigger) {
            // Handle trigger events
            for (const auto& callback : triggerCallbacks) {
                callback(collision.entityA, collision.entityB, true);
            }
        } else {
            // Resolve collision
            auto bodyA = bodyMap[collision.entityA];
            auto bodyB = bodyMap[collision.entityB];
            if (bodyA && bodyB) {
                resolveCollision(*bodyA, *bodyB, collision);
            }
            
            // Trigger collision callbacks
            for (const auto& callback : collisionCallbacks) {
                callback(collision);
            }
        }
        activeCollisionsThisFrame++;
    }
    
    // Update threading threshold based on performance
    useThreading = (bodies.size() > THREADING_THRESHOLD);
}
```

## Phase 2: Pathfinding Integration (Week 2-3)

### Step 2.1: Pathfinding Core Structures

#### Files to Create:
```
include/ai/pathfinding/
├── PathfindingGrid.hpp
├── PathfindingRequest.hpp
├── AStarNode.hpp
└── PathSmoother.hpp

src/ai/pathfinding/
├── PathfindingGrid.cpp
├── AStarNode.cpp
└── PathSmoother.cpp
```

#### PathfindingGrid.hpp:
```cpp
#pragma once
#include "../../physics/AABB.hpp"
#include "AStarNode.hpp"
#include <vector>
#include <memory>

enum class PathfindingResult {
    SUCCESS,
    NO_PATH_FOUND,
    INVALID_START,
    INVALID_GOAL,
    TIMEOUT
};

class PathfindingGrid {
private:
    int width, height;
    float cellSize;
    Vector2 worldOffset;
    
    std::vector<std::vector<bool>> obstacles; // Grid of obstacles
    std::vector<std::vector<float>> weights;  // Movement cost weights
    
    // A* working data
    std::vector<std::vector<std::unique_ptr<AStarNode>>> nodes;
    std::vector<AStarNode*> openSet;
    std::vector<AStarNode*> closedSet;
    
    // Pathfinding settings
    bool allowDiagonal = true;
    float diagonalCost = 1.414f; // sqrt(2)
    float straightCost = 1.0f;
    int maxIterations = 10000;
    
    // Internal methods
    Vector2 gridToWorld(int x, int y) const;
    std::pair<int, int> worldToGrid(const Vector2& worldPos) const;
    bool isValidGridPos(int x, int y) const;
    bool isWalkable(int x, int y) const;
    float getHeuristic(int x1, int y1, int x2, int y2) const;
    std::vector<std::pair<int, int>> getNeighbors(int x, int y) const;
    std::vector<Vector2> reconstructPath(AStarNode* goalNode) const;
    void resetNodes();
    
public:
    PathfindingGrid(int width, int height, float cellSize, const Vector2& worldOffset = {0, 0});
    ~PathfindingGrid() = default;
    
    // Grid management
    void setObstacle(int x, int y, bool isObstacle);
    void setObstacles(const std::vector<AABB>& obstacles);
    void setWeight(int x, int y, float weight);
    void clearObstacles();
    
    // Pathfinding
    PathfindingResult findPath(const Vector2& start, const Vector2& goal, std::vector<Vector2>& outPath);
    
    // Settings
    void setAllowDiagonal(bool allow) { allowDiagonal = allow; }
    void setMaxIterations(int max) { maxIterations = max; }
    void setCosts(float straight, float diagonal) { straightCost = straight; diagonalCost = diagonal; }
    
    // Query methods
    bool isObstacle(const Vector2& worldPos) const;
    float getWeight(const Vector2& worldPos) const;
    Vector2 getGridSize() const { return {static_cast<float>(width), static_cast<float>(height)}; }
    float getCellSize() const { return cellSize; }
};
```

#### PathfindingRequest.hpp:
```cpp
#pragma once
#include "../../physics/AABB.hpp"
#include <vector>
#include <functional>

enum class RequestPriority : uint8_t {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    CRITICAL = 3
};

enum class RequestStatus {
    PENDING,
    PROCESSING,
    COMPLETED,
    FAILED,
    CANCELLED
};

struct PathfindingRequest {
    uint32_t requestId;
    uint32_t entityId;
    Vector2 start;
    Vector2 goal;
    RequestPriority priority;
    RequestStatus status;
    
    // Result
    std::vector<Vector2> path;
    PathfindingResult result;
    
    // Settings
    float entityRadius = 16.0f;
    bool allowPartialPath = true;
    float maxPathLength = 2000.0f;
    
    // Timing
    float requestTime;
    float completionTime;
    
    // Callback
    std::function<void(const PathfindingRequest&)> onComplete;
    
    PathfindingRequest(uint32_t reqId, uint32_t entId, Vector2 s, Vector2 g, RequestPriority p = RequestPriority::NORMAL)
        : requestId(reqId), entityId(entId), start(s), goal(g), priority(p), status(RequestStatus::PENDING),
          result(PathfindingResult::SUCCESS), requestTime(0), completionTime(0) {}
};
```

### Step 2.2: AIManager Pathfinding Integration

#### Modify include/managers/AIManager.hpp:
```cpp
// Add includes
#include "../ai/pathfinding/PathfindingGrid.hpp"
#include "../ai/pathfinding/PathfindingRequest.hpp"

// Add to AIManager class private members:
private:
    // Pathfinding system
    std::unique_ptr<PathfindingGrid> pathfindingGrid;
    std::vector<PathfindingRequest> pendingRequests;
    std::unordered_map<uint32_t, std::vector<Vector2>> entityPaths;
    std::unordered_map<uint32_t, float> pathUpdateTimers;
    
    uint32_t nextRequestId = 1;
    static constexpr size_t PATHFINDING_THRESHOLD = 50;
    static constexpr float DEFAULT_PATH_UPDATE_INTERVAL = 0.5f;

    // Internal pathfinding methods
    void processPathfindingRequests(float deltaTime);
    void updatePathfindingObstacles();
    float getPathUpdateInterval(const AIEntity& entity) const;
    bool needsPathUpdate(uint32_t entityId, float deltaTime) const;

// Add to public methods:
public:
    // Pathfinding API
    uint32_t requestPath(uint32_t entityId, const Vector2& start, const Vector2& goal, 
                        RequestPriority priority = RequestPriority::NORMAL);
    void cancelPathRequest(uint32_t requestId);
    std::vector<Vector2> getPath(uint32_t entityId) const;
    bool hasPath(uint32_t entityId) const;
    void clearPath(uint32_t entityId);
    
    // Grid management
    void initializePathfindingGrid(int width, int height, float cellSize, const Vector2& offset = {0, 0});
    void updatePathfindingObstacles(const std::vector<AABB>& obstacles);
    
    // Path optimization
    void setPathUpdateInterval(uint32_t entityId, float interval);
    float getPathProgress(uint32_t entityId, const Vector2& currentPos) const;
```

#### Modify src/managers/AIManager.cpp:
```cpp
// Add to AIManager constructor
AIManager::AIManager() {
    // ... existing initialization ...
    
    // Initialize pathfinding grid (adjust size based on your world)
    initializePathfindingGrid(1000, 1000, 32.0f); // 1000x1000 grid, 32 pixels per cell
}

// Add pathfinding update to main update loop
void AIManager::update(float deltaTime) {
    // ... existing update code ...
    
    // Update pathfinding obstacles from physics
    updatePathfindingObstacles();
    
    // Process pathfinding requests
    processPathfindingRequests(deltaTime);
    
    // ... rest of existing update code ...
}

// Implement pathfinding methods
uint32_t AIManager::requestPath(uint32_t entityId, const Vector2& start, const Vector2& goal, RequestPriority priority) {
    uint32_t requestId = nextRequestId++;
    
    PathfindingRequest request(requestId, entityId, start, goal, priority);
    request.requestTime = SDL_GetTicks() / 1000.0f;
    
    // Insert based on priority (higher priority first)
    auto insertPos = std::upper_bound(pendingRequests.begin(), pendingRequests.end(), request,
        [](const PathfindingRequest& a, const PathfindingRequest& b) {
            return static_cast<int>(a.priority) > static_cast<int>(b.priority);
        });
    
    pendingRequests.insert(insertPos, request);
    
    return requestId;
}

void AIManager::processPathfindingRequests(float deltaTime) {
    if (pendingRequests.empty()) return;
    
    // Update path update timers
    for (auto& [entityId, timer] : pathUpdateTimers) {
        timer += deltaTime;
    }
    
    size_t requestsToProcess = std::min(pendingRequests.size(), static_cast<size_t>(10)); // Limit per frame
    
    if (useThreading && pendingRequests.size() > PATHFINDING_THRESHOLD) {
        // Use ThreadSystem for pathfinding with HIGH priority
        auto& threadSystem = GameEngine::getInstance().getThreadSystem();
        
        for (size_t i = 0; i < requestsToProcess; ++i) {
            auto& request = pendingRequests[i];
            request.status = RequestStatus::PROCESSING;
            
            threadSystem.submit(Priority::HIGH, [this, request]() mutable {
                std::vector<Vector2> path;
                request.result = pathfindingGrid->findPath(request.start, request.goal, path);
                request.path = std::move(path);
                request.status = RequestStatus::COMPLETED;
                request.completionTime = SDL_GetTicks() / 1000.0f;
                
                // Store the path
                if (request.result == PathfindingResult::SUCCESS) {
                    entityPaths[request.entityId] = request.path;
                }
                
                // Call completion callback if provided
                if (request.onComplete) {
                    request.onComplete(request);
                }
            });
        }
    } else {
        // Process on main thread
        for (size_t i = 0; i < requestsToProcess; ++i) {
            auto& request = pendingRequests[i];
            request.status = RequestStatus::PROCESSING;
            
            std::vector<Vector2> path;
            request.result = pathfindingGrid->findPath(request.start, request.goal, path);
            request.path = std::move(path);
            request.status = RequestStatus::COMPLETED;
            
            if (request.result == PathfindingResult::SUCCESS) {
                entityPaths[request.entityId] = request.path;
            }
        }
    }
    
    // Remove completed requests
    pendingRequests.erase(
        std::remove_if(pendingRequests.begin(), pendingRequests.begin() + requestsToProcess,
            [](const PathfindingRequest& req) { 
                return req.status == RequestStatus::COMPLETED || req.status == RequestStatus::FAILED; 
            }),
        pendingRequests.begin() + requestsToProcess
    );
}

void AIManager::updatePathfindingObstacles() {
    if (!pathfindingGrid) return;
    
    // Get static collision bodies from PhysicsManager
    auto& physicsManager = GameEngine::getInstance().getPhysicsManager();
    auto staticBodies = physicsManager.getStaticBodies();
    
    // Convert to AABBs and update pathfinding grid
    std::vector<AABB> obstacles;
    for (uint32_t bodyId : staticBodies) {
        // Get AABB from physics manager (you'll need to add this method)
        // obstacles.push_back(physicsManager.getBodyAABB(bodyId));
    }
    
    pathfindingGrid->setObstacles(obstacles);
}

float AIManager::getPathUpdateInterval(const AIEntity& entity) const {
    // Use existing priority system for path update frequency
    if (entity.priority >= 8) {
        return 0.1f;  // 10 times per second for critical entities
    } else if (entity.priority >= 5) {
        return 0.3f;  // ~3 times per second for important entities
    } else if (entity.priority >= 2) {
        return 0.8f;  // ~1 time per second for normal entities
    } else {
        return 2.0f;  // Every 2 seconds for background entities
    }
}
```

### Step 2.3: Behavior Integration

#### Modify existing AI behaviors to use pathfinding:

#### PatrolBehavior with Pathfinding:
```cpp
// In include/ai/behaviors/PatrolBehavior.hpp
class PatrolBehavior : public AIBehavior {
private:
    std::vector<Vector2> waypoints;
    size_t currentWaypointIndex;
    std::vector<Vector2> currentPath;
    size_t currentPathIndex;
    float pathFollowRadius;
    bool needsNewPath;
    uint32_t currentPathRequest;

public:
    void update(float deltaTime, AIEntity& entity) override {
        if (waypoints.empty()) return;
        
        Vector2 targetWaypoint = waypoints[currentWaypointIndex];
        
        // Check if we need a new path
        if (needsNewPath || currentPath.empty()) {
            auto& aiManager = GameEngine::getInstance().getAIManager();
            currentPathRequest = aiManager.requestPath(entity.id, entity.position, targetWaypoint, 
                                                     static_cast<RequestPriority>(entity.priority / 3));
            needsNewPath = false;
        }
        
        // Follow current path
        auto& aiManager = GameEngine::getInstance().getAIManager();
        currentPath = aiManager.getPath(entity.id);
        
        if (!currentPath.empty()) {
            followPath(deltaTime, entity);
        }
        
        // Check if reached current waypoint
        if (Vector2::distance(entity.position, targetWaypoint) < 32.0f) {
            currentWaypointIndex = (currentWaypointIndex + 1) % waypoints.size();
            needsNewPath = true;
            currentPath.clear();
        }
    }
    
private:
    void followPath(float deltaTime, AIEntity& entity) {
        if (currentPathIndex >= currentPath.size()) return;
        
        Vector2 targetPoint = currentPath[currentPathIndex];
        Vector2 direction = (targetPoint - entity.position).normalized();
        
        entity.position = entity.position + direction * entity.speed * deltaTime;
        
        // Check if reached current path point
        if (Vector2::distance(entity.position, targetPoint) < pathFollowRadius) {
            currentPathIndex++;
        }
        
        // If reached end of path, clear it
        if (currentPathIndex >= currentPath.size()) {
            currentPath.clear();
            currentPathIndex = 0;
        }
    }
};
```

#### ChaseBehavior with Dynamic Pathfinding:
```cpp
// In include/ai/behaviors/ChaseBehavior.hpp
class ChaseBehavior : public AIBehavior {
private:
    uint32_t targetEntityId;
    std::vector<Vector2> pathToTarget;
    float pathRecalculateTimer;
    float pathRecalculateInterval;
    uint32_t currentPathRequest;
    bool hasValidPath;

public:
    void update(float deltaTime, AIEntity& entity) override {
        pathRecalculateTimer += deltaTime;
        
        Vector2 targetPos = getTargetPosition(); // Implement based on your target system
        
        // Request new path if needed
        if (pathRecalculateTimer >= pathRecalculateInterval || !hasValidPath) {
            auto& aiManager = GameEngine::getInstance().getAIManager();
            currentPathRequest = aiManager.requestPath(entity.id, entity.position, targetPos, 
                                                     RequestPriority::HIGH); // Chase is high priority
            pathRecalculateTimer = 0.0f;
        }
        
        // Follow current path
        auto& aiManager = GameEngine::getInstance().getAIManager();
        pathToTarget = aiManager.getPath(entity.id);
        hasValidPath = !pathToTarget.empty();
        
        if (hasValidPath) {
            followPathToTarget(deltaTime, entity);
        } else {
            // Fallback: move directly toward target (for immediate response)
            Vector2 direction = (targetPos - entity.position).normalized();
            entity.position = entity.position + direction * entity.speed * deltaTime * 0.5f; // Slower fallback
        }
    }
    
private:
    void followPathToTarget(float deltaTime, AIEntity& entity) {
        // Similar to PatrolBehavior::followPath but optimized for chasing
        // Implementation details...
    }
};
```
