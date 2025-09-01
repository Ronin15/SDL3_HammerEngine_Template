// Internal path-following helper (non-public). Kept under src/.
#ifndef AI_INTERNAL_PATHFOLLOW_HPP
#define AI_INTERNAL_PATHFOLLOW_HPP

#include <SDL3/SDL.h>
#include <vector>
#include "utils/Vector2D.hpp"
#include "entities/Entity.hpp"

namespace AIInternal {

// Unified cooldown management to prevent overlapping backoffs
struct CooldownState {
    Uint64 nextPathRequest{0};
    Uint64 stallRecoveryUntil{0};
    Uint64 behaviorChangeUntil{0};
    
    bool canRequestPath(Uint64 now) const {
        return now >= nextPathRequest && now >= stallRecoveryUntil;
    }
    
    bool canChangeBehavior(Uint64 now) const {
        return now >= behaviorChangeUntil;
    }
    
    void applyPathCooldown(Uint64 now, Uint64 cooldownMs = 800) {
        nextPathRequest = now + cooldownMs;
    }
    
    void applyStallCooldown(Uint64 now, Uint64 stallId = 0) {
        stallRecoveryUntil = now + 250 + (stallId % 400);
    }
    
    void applyBehaviorCooldown(Uint64 now, Uint64 cooldownMs = 500) {
        behaviorChangeUntil = now + cooldownMs;
    }
};

struct PathPolicy {
    Uint64 pathTTL{3000};            // ms - increased from 1500 to reduce refresh frequency
    Uint64 noProgressWindow{800};    // ms - increased from 300 to be more patient
    float nodeRadius{16.0f};
    bool allowDetours{true};
    // Detour sampling
    float detourAngles[4]{0.35f, -0.35f, 0.7f, -0.7f};
    float detourRadii[2]{80.0f, 140.0f};
    // Lateral lane bias while following (0=off)
    float lateralBias{0.0f}; // 0.0..~0.25
    
    // Adaptive stall detection
    float getStallThreshold(float entitySpeed) const {
        return std::max(1.0f, entitySpeed * 0.6f);
    }
    
    Uint64 getStallTimeThreshold(float entitySpeed) const {
        return static_cast<Uint64>(800 + (entitySpeed * 100));
    }
};

// Clamp a world-space point within current world bounds (with margin)
Vector2D ClampToWorld(const Vector2D &p, float margin = 100.0f);

// Get world bounds in pixel coordinates (converts tile bounds to world bounds)
struct WorldBoundsPixels { float minX, minY, maxX, maxY; bool valid; };
WorldBoundsPixels GetWorldBoundsInPixels();

// Refresh path with policy: returns true if a (possibly new) path is ready.
bool RefreshPathWithPolicy(
    EntityPtr entity,
    const Vector2D &currentPos,
    const Vector2D &desiredGoal,
    std::vector<Vector2D> &pathPoints,
    size_t &currentPathIndex,
    Uint64 &lastPathUpdate,
    Uint64 &lastProgressTime,
    float &lastNodeDistance,
    const PathPolicy &policy);

// Follow current path one step, applying optional lateral bias; returns true if following.
bool FollowPathStepWithPolicy(
    EntityPtr entity,
    const Vector2D &currentPos,
    std::vector<Vector2D> &pathPoints,
    size_t &currentPathIndex,
    float speed,
    float nodeRadius,
    float lateralBias);

// Async pathfinding support - refresh path with async requests
bool RefreshPathWithPolicyAsync(
    EntityPtr entity,
    const Vector2D &currentPos,
    const Vector2D &desiredGoal,
    std::vector<Vector2D> &pathPoints,
    size_t &currentPathIndex,
    Uint64 &lastPathUpdate,
    Uint64 &lastProgressTime,
    float &lastNodeDistance,
    const PathPolicy &policy,
    int priority = 1); // 0=Critical, 1=High, 2=Normal, 3=Low

// Yield and redirect behavior for face-offs
struct YieldResult {
    bool shouldYield{false};
    bool shouldRedirect{false};
    Vector2D redirectDirection{0, 0};
    Uint64 yieldDuration{0}; // ms to wait
};

// Check if entity should yield to other NPCs or redirect around them
YieldResult CheckYieldAndRedirect(
    EntityPtr entity,
    const Vector2D &currentPos,
    const Vector2D &intendedDirection,
    float intendedSpeed);

// Apply yielding behavior - returns true if entity should stop/slow down
bool ApplyYieldBehavior(
    EntityPtr entity,
    const YieldResult &yieldResult,
    Uint64 &yieldStartTime,
    Uint64 currentTime);

// Dynamic stuck detection and escape system
struct StuckDetectionState {
    Vector2D lastPosition{0, 0};
    Uint64 lastMovementTime{0};
    Uint64 stuckStartTime{0};
    bool isCurrentlyStuck{false};
    int escapeAttempts{0};
    
    void updatePosition(const Vector2D& newPos, Uint64 currentTime) {
        float movement = (newPos - lastPosition).length();
        if (movement > 2.0f) { // Meaningful movement threshold
            lastMovementTime = currentTime;
            isCurrentlyStuck = false;
            stuckStartTime = 0;
            escapeAttempts = 0;
        }
        lastPosition = newPos;
    }
    
    bool checkIfStuck(EntityPtr entity, Uint64 currentTime) {
        if (!entity) return false;
        
        Vector2D currentVel = entity->getVelocity();
        float velMagnitude = currentVel.length();
        
        // Has velocity but hasn't moved recently
        if (velMagnitude > 5.0f && (currentTime - lastMovementTime) > 400) {
            if (!isCurrentlyStuck) {
                stuckStartTime = currentTime;
                isCurrentlyStuck = true;
            }
            return true;
        }
        
        return false;
    }
    
    bool needsEscape(Uint64 currentTime) {
        return isCurrentlyStuck && (currentTime - stuckStartTime) > static_cast<Uint64>(200 + escapeAttempts * 150);
    }
};

// Check if entity is stuck and apply dynamic escape
bool HandleDynamicStuckDetection(
    EntityPtr entity,
    StuckDetectionState &stuckState,
    Uint64 currentTime);

} // namespace AIInternal

#endif // AI_INTERNAL_PATHFOLLOW_HPP

