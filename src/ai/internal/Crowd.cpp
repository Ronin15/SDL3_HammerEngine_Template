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

  HammerEngine::AABB area(currentPos.getX(), currentPos.getY(), radius, radius);
  cm.queryArea(area, queryResults);

  Vector2D sep(0, 0);
  float closest = radius;
  size_t counted = 0;
  for (EntityID id : queryResults) {
    if (id == entity->getID()) continue;
    if (!cm.isDynamic(id) || cm.isTrigger(id)) continue;
    Vector2D other;
    if (!cm.getBodyCenter(id, other)) continue;
    Vector2D d = currentPos - other;
    float dist = d.length();
    if (dist < 1.0f) continue;
    closest = std::min(closest, dist);
    float w = std::max(0.0f, (radius - dist) / radius); // stronger when closer
    sep = sep + (d * (1.0f / dist)) * w;
    if (++counted >= maxNeighbors) break;
  }

  Vector2D out = intendedVel;
  float il = out.length();
  if (sep.length() > 0.001f && closest < radius * 0.9f && il > 0.001f) {
    // Blend intended + separation and renormalize to speed
    Vector2D blended = out + sep * strength * speed;
    float bl = blended.length();
    if (bl > 0.01f) out = blended * (speed / bl);
  }
  return out;
}

} // namespace AIInternal

