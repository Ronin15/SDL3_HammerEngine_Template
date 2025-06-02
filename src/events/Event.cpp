/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "events/Event.hpp"

void Event::startCooldown() {
    if (m_cooldownTime <= 0.0f) {
        return; // No cooldown set
    }

    m_onCooldown = true;
    m_cooldownTimer = 0.0f;
}

bool Event::shouldUpdate() const {
    // If the event is not active, don't update
    if (!m_active) {
        return false;
    }

    // If it's a one-time event that has already triggered, don't update
    if (m_oneTimeEvent && m_hasTriggered) {
        return false;
    }

    // If it's on cooldown, don't update
    if (m_onCooldown) {
        return false;
    }

    // Handle update frequency (only update every m_updateFrequency frames)
    if (m_updateFrequency > 1) {
        m_frameCounter++;
        if (m_frameCounter % m_updateFrequency != 0) {
            return false;
        }
    }

    return true;
}

void Event::updateCooldown(float deltaTime) {
    if (!m_onCooldown) {
        return;
    }

    m_cooldownTimer += deltaTime;

    if (m_cooldownTimer >= m_cooldownTime) {
        m_onCooldown = false;
        m_cooldownTimer = 0.0f;
    }
}
