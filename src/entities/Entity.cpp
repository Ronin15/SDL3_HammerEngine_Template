/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/Entity.hpp"

void Entity::playAnimation(const std::string& animName) {
    auto it = m_animationMap.find(animName);
    if (it != m_animationMap.end()) {
        const auto& config = it->second;
        m_currentRow = config.row + 1;  // TextureManager uses 1-based rows
        m_numFrames = config.frameCount;
        m_animSpeed = config.speed;
        m_animationLoops = config.loop;
        m_currentFrame = 0;
    }
}
