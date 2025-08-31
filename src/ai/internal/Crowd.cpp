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

  // Much larger query area for world-scale separation
  // Scale separation radius based on speed for dynamic behavior
  float baseRadius = std::max(radius, 64.0f); // Minimum 64px separation
  float speedMultiplier = std::clamp(speed / 100.0f, 1.0f, 3.0f); // Scale 1x-3x based on speed
  float queryRadius = baseRadius * speedMultiplier * 2.0f; // Much larger search area
  
  HammerEngine::AABB area(currentPos.getX() - queryRadius, currentPos.getY() - queryRadius, 
                          queryRadius * 2.0f, queryRadius * 2.0f);
  cm.queryArea(area, queryResults);

  Vector2D sep(0, 0);
  Vector2D avoidance(0, 0);
  Vector2D longRangeSep(0, 0); // New: long-range separation for world spreading
  float closest = queryRadius;
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
    
    // Multi-layer separation with world-scale awareness
    Vector2D dir = d * (1.0f / dist);
    
    if (dist < baseRadius * 0.5f) {
      // Critical range - strong repulsion
      float criticalWeight = (baseRadius * 0.5f - dist) / (baseRadius * 0.5f);
      avoidance = avoidance + dir * (criticalWeight * criticalWeight * 3.0f); // Increased strength
      criticalNeighbors++;
    } else if (dist < baseRadius) {
      // Normal separation range
      float normalWeight = (baseRadius - dist) / baseRadius;
      sep = sep + dir * normalWeight;
    } else if (dist < baseRadius * 3.0f) {
      // Medium-range gentle separation for world spreading
      float mediumWeight = (baseRadius * 3.0f - dist) / (baseRadius * 2.0f);
      longRangeSep = longRangeSep + dir * (mediumWeight * 0.4f);
    } else {
      // Long-range awareness for early spread behavior
      float longWeight = (queryRadius - dist) / (queryRadius - baseRadius * 3.0f);
      longRangeSep = longRangeSep + dir * (longWeight * 0.1f);
    }
    
    if (++counted >= maxNeighbors * 2) break; // Allow more neighbors for world-scale
  }

  Vector2D out = intendedVel;
  float il = out.length();
  
  if ((sep.length() > 0.001f || avoidance.length() > 0.001f || longRangeSep.length() > 0.001f) && il > 0.001f) {
    Vector2D intendedDir = out * (1.0f / il);
    
    // Emergency avoidance with pathfinding preservation
    if (criticalNeighbors > 0 && avoidance.length() > 0.001f) {
      // Instead of abandoning the path, find perpendicular avoidance that preserves forward progress
      Vector2D avoidDir = avoidance.normalized();
      Vector2D perpendicular(-intendedDir.getY(), intendedDir.getX());
      
      // Choose perpendicular direction that aligns better with avoidance
      if (avoidDir.dot(perpendicular) < 0) {
        perpendicular = Vector2D(intendedDir.getY(), -intendedDir.getX());
      }
      
      // Blend forward movement with perpendicular avoidance
      Vector2D emergencyVel = intendedDir * 0.6f + perpendicular * 0.8f; // Preserve 60% forward progress
      float emLen = emergencyVel.length();
      if (emLen > 0.01f) {
        out = emergencyVel * (speed / emLen);
      }
    } else {
      // Pathfinding-aware separation that preserves goal direction
      float adaptiveStrength = strength;
      
      // Dynamic strength adjustment based on crowd density
      if (counted >= maxNeighbors) {
        adaptiveStrength = std::min(adaptiveStrength * 1.5f, 0.6f); // Cap max separation influence
      }
      if (closest < baseRadius * 0.7f) {
        adaptiveStrength = std::min(adaptiveStrength * 1.3f, 0.5f);
      }
      
      // Pathfinding-preserving separation strategy
      if (sep.length() > 0.001f) {
        Vector2D sepDir = sep.normalized();
        
        // Check if separation conflicts with intended direction
        float directionConflict = -sepDir.dot(intendedDir); // How much separation opposes forward movement
        
        if (directionConflict > 0.7f) {
          // High conflict: apply lateral redirection instead of opposing force
          Vector2D lateral(-intendedDir.getY(), intendedDir.getX());
          if (sepDir.dot(lateral) < 0) {
            lateral = Vector2D(intendedDir.getY(), -intendedDir.getX());
          }
          
          // Blend forward movement with lateral avoidance
          Vector2D redirected = intendedDir * 0.75f + lateral * adaptiveStrength * 2.0f;
          float redirLen = redirected.length();
          if (redirLen > 0.01f) {
            out = redirected * (speed / redirLen);
          }
        } else {
          // Low conflict: apply gentle separation with strong forward bias
          Vector2D forwardBias = out * (1.0f - adaptiveStrength * 0.5f); // Preserve more forward momentum
          Vector2D separationForce = sep * adaptiveStrength * speed * 0.7f; // Reduce separation strength
          Vector2D blended = forwardBias + separationForce;
          
          float bl = blended.length();
          if (bl > 0.01f) {
            out = blended * (speed / bl);
          }
        }
      }
      
      // Apply world-scale spreading as gentle bias only
      if (longRangeSep.length() > 0.001f) {
        Vector2D spreadBias = longRangeSep * 0.15f * speed; // Much gentler influence
        out = out + spreadBias;
        float finalLen = out.length();
        if (finalLen > speed) {
          out = out * (speed / finalLen);
        }
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

