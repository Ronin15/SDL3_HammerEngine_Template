/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/Pet.hpp"
#include "collisions/CollisionBody.hpp"
#include "managers/CollisionManager.hpp"
#include "core/Logger.hpp"

Pet::Pet(const std::string &textureID, const Vector2D &startPosition,
         int frameWidth, int frameHeight)
    : NPC(textureID, startPosition, frameWidth, frameHeight) {
}

void Pet::ensurePhysicsBodyRegistered() {
    auto &cm = CollisionManager::Instance();
    const float halfW = m_frameWidth > 0 ? m_frameWidth * 0.5f : 16.0f;
    const float halfH = m_height > 0 ? m_height * 0.5f : 16.0f;
    HammerEngine::AABB aabb(m_position.getX(), m_position.getY(), halfW, halfH);

    uint32_t layer = HammerEngine::CollisionLayer::Layer_Pet;
    uint32_t mask = 0xFFFFFFFFu & ~(HammerEngine::CollisionLayer::Layer_Player |
                                     HammerEngine::CollisionLayer::Layer_Pet);

    cm.addCollisionBodySOA(getID(), aabb.center, aabb.halfSize,
                           HammerEngine::BodyType::KINEMATIC, layer, mask);
    cm.processPendingCommands();
    cm.attachEntity(getID(), shared_this());
}
