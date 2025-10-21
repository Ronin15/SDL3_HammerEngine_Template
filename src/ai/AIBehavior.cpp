/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "ai/AIBehavior.hpp"
#include "managers/PathfinderManager.hpp"

AIBehavior::~AIBehavior() = default;

void AIBehavior::cleanupEntity(EntityPtr entity) {
    // Default implementation does nothing
    (void)entity;
}

PathfinderManager& AIBehavior::pathfinder() const {
    // Static reference cached on first use - eliminates repeated Instance() calls
    static PathfinderManager& pf = PathfinderManager::Instance();
    return pf;
}



