#include "ai/internal/Crowd.hpp"
#include "managers/CollisionManager.hpp"
#include <algorithm>
#include <cmath>

namespace AIInternal {

Vector2D ApplySeparation(EntityPtr entity,
                         const Vector2D &currentPos,
                         const Vector2D &intendedVel,
                         float speed,
                         float radius,
                         float strength,
                         size_t maxNeighbors) {
  if (!entity || speed <= 0.0f) return intendedVel;

  auto &cm = CollisionManager::Instance();

  // Use thread-local vector to avoid repeated allocations
  static thread_local std::vector<EntityID> queryResults;
  queryResults.clear();

  // Larger query area for better early separation
  float queryRadius = radius * 1.4f;
  HammerEngine::AABB area(currentPos.getX() - queryRadius, currentPos.getY() - queryRadius, 
                          queryRadius * 2.0f, queryRadius * 2.0f);
  cm.queryArea(area, queryResults);

  Vector2D sep(0, 0);
  Vector2D avoidance(0, 0);
  float closest = radius;
  size_t counted = 0;
  size_t criticalNeighbors = 0;
  
  for (EntityID id : queryResults) {
    if (id == entity->getID()) continue;
    if (!cm.isDynamic(id) || cm.isTrigger(id)) continue;
    Vector2D other;
    if (!cm.getBodyCenter(id, other)) continue;
    Vector2D d = currentPos - other;
    float dist = d.length();
    if (dist < 0.5f) {
      // Handle extreme overlap with emergency push
      d = Vector2D((rand() % 200 - 100) / 100.0f, (rand() % 200 - 100) / 100.0f);
      dist = 16.0f;
      criticalNeighbors++;
    }
    if (dist > queryRadius) continue;
    
    closest = std::min(closest, dist);
    
    // Multi-layer separation with different behaviors at different distances
    Vector2D dir = d * (1.0f / dist);
    
    if (dist < radius * 0.5f) {
      // Critical range - strong repulsion
      float criticalWeight = (radius * 0.5f - dist) / (radius * 0.5f);
      avoidance = avoidance + dir * (criticalWeight * criticalWeight * 2.0f);
      criticalNeighbors++;
    } else if (dist < radius) {
      // Normal separation range
      float normalWeight = (radius - dist) / radius;
      sep = sep + dir * normalWeight;
    } else {
      // Early warning range - gentle nudging
      float earlyWeight = (queryRadius - dist) / (queryRadius - radius);
      sep = sep + dir * (earlyWeight * 0.3f);
    }
    
    if (++counted >= maxNeighbors) break;
  }

  Vector2D out = intendedVel;
  float il = out.length();
  
  if ((sep.length() > 0.001f || avoidance.length() > 0.001f) && il > 0.001f) {
    // Emergency avoidance takes priority
    if (criticalNeighbors > 0 && avoidance.length() > 0.001f) {
      // Strong avoidance with some forward momentum preservation
      Vector2D emergencyVel = avoidance * speed * 1.5f + out * 0.2f;
      float emLen = emergencyVel.length();
      if (emLen > 0.01f) {
        out = emergencyVel * (speed / emLen);
      }
    } else if (sep.length() > 0.001f) {
      // Enhanced separation blending with momentum preservation
      float adaptiveStrength = strength;
      
      // Increase strength based on crowding
      if (counted >= maxNeighbors) {
        adaptiveStrength *= 1.5f;
      }
      if (closest < radius * 0.7f) {
        adaptiveStrength *= 1.3f;
      }
      
      // Preserve some forward momentum while adding separation
      Vector2D forwardBias = out * (1.0f - adaptiveStrength);
      Vector2D separationForce = sep * adaptiveStrength * speed;
      Vector2D blended = forwardBias + separationForce;
      
      float bl = blended.length();
      if (bl > 0.01f) {
        out = blended * (speed / bl);
      }
    }
  }
  
  return out;
}

Vector2D SmoothVelocityTransition(const Vector2D &currentVel,
                                  const Vector2D &targetVel,
                                  float smoothingFactor,
                                  float maxChange) {
  Vector2D deltaVel = targetVel - currentVel;
  float deltaLen = deltaVel.length();
  
  if (deltaLen <= 0.01f) {
    return currentVel; // No significant change needed
  }
  
  // Limit maximum change per frame to prevent jitter
  if (deltaLen > maxChange) {
    deltaVel = deltaVel * (maxChange / deltaLen);
  }
  
  // Apply smoothing - smaller factor = more smoothing
  Vector2D smoothedDelta = deltaVel * smoothingFactor;
  Vector2D result = currentVel + smoothedDelta;
  
  // Ensure the result maintains reasonable bounds
  float resultLen = result.length();
  if (resultLen > 200.0f) { // Cap maximum velocity
    result = result * (200.0f / resultLen);
  }
  
  return result;
}

} // namespace AIInternal

