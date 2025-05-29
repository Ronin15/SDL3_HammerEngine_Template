/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/AIBehavior.hpp"

// Remove an entity from any behavior-specific cleanup
void AIBehavior::cleanupEntity(EntityPtr entity) {
    // Base implementation - behavior implementations can override for specific cleanup
    (void)entity; // Unused parameter
}