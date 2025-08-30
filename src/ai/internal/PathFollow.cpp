#include "ai/internal/PathFollow.hpp"
#include "managers/AIManager.hpp"
#include "managers/WorldManager.hpp"
#include <algorithm>
#include <cmath>

namespace AIInternal {

Vector2D ClampToWorld(const Vector2D &p, float margin) {
    float minX, minY, maxX, maxY;
    if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
        const float TILE = 32.0f;
        float worldMinX = minX * TILE + margin;
        float worldMinY = minY * TILE + margin;
        float worldMaxX = maxX * TILE - margin;
        float worldMaxY = maxY * TILE - margin;
        Vector2D clamped(
            std::clamp(p.getX(), worldMinX, worldMaxX),
            std::clamp(p.getY(), worldMinY, worldMaxY));
        return clamped;
    }
    return p;
}

static void requestTo(EntityPtr entity,
                      const Vector2D &from,
                      const Vector2D &goal,
                      std::vector<Vector2D> &outPath,
                      size_t &idx,
                      Uint64 &lastUpdate,
                      Uint64 now,
                      Uint64 &lastProgress,
                      float &lastNodeDist) {
    AIManager::Instance().requestPath(entity, from, goal);
    outPath = AIManager::Instance().getPath(entity);
    idx = 0;
    lastUpdate = now;
    lastNodeDist = std::numeric_limits<float>::infinity();
    lastProgress = now;
}

bool RefreshPathWithPolicy(
    EntityPtr entity,
    const Vector2D &currentPos,
    const Vector2D &desiredGoal,
    std::vector<Vector2D> &pathPoints,
    size_t &currentPathIndex,
    Uint64 &lastPathUpdate,
    Uint64 &lastProgressTime,
    float &lastNodeDistance,
    const PathPolicy &policy) {

    Uint64 now = SDL_GetTicks();
    bool needRefresh = pathPoints.empty() || currentPathIndex >= pathPoints.size();
    if (!needRefresh && currentPathIndex < pathPoints.size()) {
        float d = (pathPoints[currentPathIndex] - currentPos).length();
        if (d + 1.0f < lastNodeDistance) { lastNodeDistance = d; lastProgressTime = now; }
        else if (lastProgressTime == 0) { lastProgressTime = now; }
        else if (now - lastProgressTime > policy.noProgressWindow) { needRefresh = true; }
    }
    if (now - lastPathUpdate > policy.pathTTL) needRefresh = true;
    if (!needRefresh) return false;

    // Clamp desired goal first
    Vector2D clampedGoal = ClampToWorld(desiredGoal);
    requestTo(entity, currentPos, clampedGoal, pathPoints, currentPathIndex, lastPathUpdate, now, lastProgressTime, lastNodeDistance);
    if (!pathPoints.empty() || !policy.allowDetours) return true;

    // Try detours around the goal
    for (float r : policy.detourRadii) {
        for (float a : policy.detourAngles) {
            Vector2D offset(std::cos(a) * r, std::sin(a) * r);
            Vector2D alt = ClampToWorld(clampedGoal + offset);
            requestTo(entity, currentPos, alt, pathPoints, currentPathIndex, lastPathUpdate, now, lastProgressTime, lastNodeDistance);
            if (!pathPoints.empty()) return true;
        }
    }
    return true; // refreshed, but path may still be empty
}

bool FollowPathStepWithPolicy(
    EntityPtr entity,
    const Vector2D &currentPos,
    std::vector<Vector2D> &pathPoints,
    size_t &currentPathIndex,
    float speed,
    float nodeRadius,
    float lateralBias) {
    if (pathPoints.empty() || currentPathIndex >= pathPoints.size()) return false;
    Vector2D node = pathPoints[currentPathIndex];
    Vector2D dir = node - currentPos;
    float len = dir.length();
    if (len > 0.01f) {
        dir = dir * (1.0f / len);
        if (lateralBias > 0.0f) {
            Vector2D perp(-dir.getY(), dir.getX());
            float side = ((entity->getID() & 1) ? 1.0f : -1.0f);
            Vector2D biased = dir * 1.0f + perp * (lateralBias * side);
            float bl = biased.length(); if (bl > 0.001f) biased = biased * (1.0f / bl);
            entity->setVelocity(biased * speed);
        } else {
            entity->setVelocity(dir * speed);
        }
    }
    if ((node - currentPos).length() <= nodeRadius) {
        ++currentPathIndex;
    }
    return true;
}

} // namespace AIInternal

