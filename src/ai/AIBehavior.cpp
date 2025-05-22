/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/AIBehavior.hpp"
#include "core/GameEngine.hpp"
#include "managers/GameStateManager.hpp"
#include "gameStates/GameState.hpp"
#include "gameStates/AIDemoState.hpp"

bool AIBehavior::isWithinUpdateFrequency() const {
    // Always update if frequency is 1
    if (m_updateFrequency <= 1) {
        return true;
    }
    
    // Update if enough frames have passed
    return (m_framesSinceLastUpdate % m_updateFrequency == 0);
}

Entity* AIBehavior::findPlayerEntity() const {
    // Get the game state manager
    GameStateManager* gameStateManager = GameEngine::Instance().getGameStateManager();
    if (!gameStateManager) return nullptr;
    
    // Try to get the AIDemoState which contains the player
    GameState* currentState = gameStateManager->getState("AIDemo");
    if (!currentState) {
        // If we're not in AIDemo, try to check other states that might have a player
        // For now we just return nullptr as we don't have access to other game states' player
        return nullptr;
    }
    
    // Cast to AIDemoState to access the player
    AIDemoState* aiDemoState = dynamic_cast<AIDemoState*>(currentState);
    if (!aiDemoState) return nullptr;
    
    // Get the player from the state
    return aiDemoState->getPlayer();
}

bool AIBehavior::shouldUpdate(Entity* entity) const {
    // Base check - if not active, don't update
    if (!m_active) return false;

    // Frequency check - important to respect update frequency
    if (!isWithinUpdateFrequency()) return false;

    // Always update high priority behaviors
    if (m_priority > 8) return true;

    // If no entity, can't do distance check
    if (!entity) return true;

    // Find the player entity
    Entity* player = findPlayerEntity();

    // If no player was found, fall back to distance from origin
    if (!player) {
        Vector2D position = entity->getPosition();
        float distSq = position.lengthSquared();

        // Use priority-based update frequency for fallback
        float priorityMultiplier = (m_priority + 1) / 10.0f;

        if (distSq < m_maxUpdateDistance * m_maxUpdateDistance * priorityMultiplier) {
            return true; // Update every frame
        }

        if (distSq < m_mediumUpdateDistance * m_mediumUpdateDistance * priorityMultiplier) {
            return (m_framesSinceLastUpdate % 3 == 0); // Update every 3 frames
        }

        if (distSq < m_minUpdateDistance * m_minUpdateDistance * priorityMultiplier) {
            return (m_framesSinceLastUpdate % 5 == 0); // Update every 5 frames
        }

        return (m_framesSinceLastUpdate % 10 == 0); // Update every 10 frames
    }

    // This section runs when player is found

    // Calculate squared distance to player (more efficient than using length())
    Vector2D toPlayer = player->getPosition() - entity->getPosition();
    float distSq = toPlayer.lengthSquared();

    // Determine update frequency based on distance to player and priority
    float priorityMultiplier = (m_priority + 1) / 10.0f; // Convert 0-9 priority to 0.1-1.0 multiplier

    // Close to player - update every frame
    if (distSq < m_maxUpdateDistance * m_maxUpdateDistance * priorityMultiplier) {
        return true;
    }

    // Medium distance from player - update less frequently
    if (distSq < m_mediumUpdateDistance * m_mediumUpdateDistance * priorityMultiplier) {
        return (m_framesSinceLastUpdate % 3 == 0); // Update every 3 frames
    }

    // Far from player - update rarely
    if (distSq < m_minUpdateDistance * m_minUpdateDistance * priorityMultiplier) {
        return (m_framesSinceLastUpdate % 5 == 0); // Update every 5 frames
    }

    // Very far from player - update very rarely
    return (m_framesSinceLastUpdate % 10 == 0); // Update every 10 frames
}
