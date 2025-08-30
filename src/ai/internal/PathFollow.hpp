// Internal path-following helper (non-public). Kept under src/.
#ifndef AI_INTERNAL_PATHFOLLOW_HPP
#define AI_INTERNAL_PATHFOLLOW_HPP

#include <SDL3/SDL.h>
#include <vector>
#include "utils/Vector2D.hpp"
#include "entities/Entity.hpp"

namespace AIInternal {

struct PathPolicy {
    Uint64 pathTTL{1500};            // ms
    Uint64 noProgressWindow{300};    // ms
    float nodeRadius{16.0f};
    bool allowDetours{true};
    // Detour sampling
    float detourAngles[4]{0.35f, -0.35f, 0.7f, -0.7f};
    float detourRadii[2]{80.0f, 140.0f};
    // Lateral lane bias while following (0=off)
    float lateralBias{0.0f}; // 0.0..~0.25
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

} // namespace AIInternal

#endif // AI_INTERNAL_PATHFOLLOW_HPP

