/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef COLLISION_BODY_HPP
#define COLLISION_BODY_HPP

#include <cstdint>
#include "collisions/AABB.hpp"
#include "collisions/TriggerTag.hpp"
#include "entities/Entity.hpp" // for EntityID alias & EntityWeakPtr

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
    Vector2D lastPosition{-1.0f, -1.0f}; // Track previous position for movement optimization
    EntityWeakPtr entityWeak; // optional back-reference for syncing
    BodyType type{BodyType::DYNAMIC};
    uint32_t layer{Layer_Default};
    uint32_t collidesWith{0xFFFFFFFFu};
    bool enabled{true};
    bool isTrigger{false};
    TriggerTag triggerTag{TriggerTag::None};
    float mass{1.0f};
    float friction{0.8f};
    float restitution{0.0f};

    bool shouldCollideWith(const CollisionBody& other) const {
        return enabled && other.enabled && (collidesWith & other.layer) != 0u;
    }
};

} // namespace HammerEngine

#endif // COLLISION_BODY_HPP
