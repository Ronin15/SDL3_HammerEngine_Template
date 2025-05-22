/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/AIBehavior.hpp"

// Mock implementation for testing - doesn't rely on game engine components
bool AIBehavior::isWithinUpdateFrequency() const {
    // In test environment, always return true to ensure behaviors update
    return true;
}

Entity* AIBehavior::findPlayerEntity() const {
    // For tests, we don't need to find a real player entity
    return nullptr;
}

bool AIBehavior::shouldUpdate(Entity* /* entity */) const {
    // In test environment, always return true to ensure behaviors update
    return true;
}