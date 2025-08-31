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
Vector2D ClampToWorld(const Vector2D &p, float margin = 16.0f);

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

} // namespace AIInternal

#endif // AI_INTERNAL_PATHFOLLOW_HPP

