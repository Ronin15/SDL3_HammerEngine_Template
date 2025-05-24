/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/AIBehavior.hpp"

// Mock implementation for testing - doesn't rely on game engine components
bool AIBehavior::isWithinUpdateFrequency(EntityPtr entity) const {
    // In test environment, handle both cases:
    // 1. If frequency is 1, always update
    if (m_updateFrequency <= 1) {
        return true;
    }
    
    // 2. For benchmarks, respect the frame counter using thread-safe method
    if (entity) {
        int frameCounter = getFrameCounter(entity);
        return frameCounter >= m_updateFrequency;
    }
    
    // Default case: allow update
    return true;
}

EntityPtr AIBehavior::findPlayerEntity() {
    // For tests, we don't need to find a real player entity
    return nullptr;
}

bool AIBehavior::shouldUpdate(EntityPtr entity) const {
    // Base check - if not active, don't update
    if (!m_active) return false;
    
    // For benchmarks, we need to respect the update frequency
    if (!isWithinUpdateFrequency(entity)) return false;
    
    // Otherwise, allow updates for all test entities
    return true;
}

void AIBehavior::cleanupEntity(EntityPtr entity) {
    // Remove entity from frame counter map with proper locking
    if (entity) {
        std::lock_guard<std::mutex> lock(m_frameCounterMutex);
        m_entityFrameCounters.erase(entity);
    }
}