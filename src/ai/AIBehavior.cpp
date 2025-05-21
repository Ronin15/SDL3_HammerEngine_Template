/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "ai/AIBehavior.hpp"

bool AIBehavior::isWithinUpdateFrequency() const {
    // Always update if frequency is 1
    if (m_updateFrequency <= 1) {
        return true;
    }
    
    // Update if enough frames have passed
    return (m_framesSinceLastUpdate % m_updateFrequency == 0);
}