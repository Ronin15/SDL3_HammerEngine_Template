/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef COLLISION_INFO_HPP
#define COLLISION_INFO_HPP

#include "entities/Entity.hpp"
#include "collisions/AABB.hpp"

namespace HammerEngine {

struct CollisionInfo {
    EntityID a{0};
    EntityID b{0};
    Vector2D normal{0,0};
    float penetration{0.0f};
    bool trigger{false};

    // Performance optimization: Store SOA indices to eliminate linear lookups
    size_t indexA{SIZE_MAX};
    size_t indexB{SIZE_MAX};
};

} // namespace HammerEngine

#endif // COLLISION_INFO_HPP
