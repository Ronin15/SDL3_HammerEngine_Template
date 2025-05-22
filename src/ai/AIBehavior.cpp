/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/AIBehavior.hpp"
#include "core/GameEngine.hpp"
#include "managers/GameStateManager.hpp"
#include "gameStates/GameState.hpp"
#include "gameStates/AIDemoState.hpp"

bool AIBehavior::isWithinUpdateFrequency(Entity* entity) const {
    // No entity check
    if (!entity) {
        return true;
    }
    
    // Always update if frequency is 1
    if (m_updateFrequency <= 1) {
        return true;
    }
    
    // Update if enough frames have passed
    // Get or create frame counter for this entity
    int& frameCounter = m_entityFrameCounters[entity];
    return (frameCounter >= m_updateFrequency);
}

// Remove an entity from the frame counter map
void AIBehavior::cleanupEntity(Entity* entity) {
    if (entity) {
        m_entityFrameCounters.erase(entity);
    }
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

    // If no entity, can't do distance check
    if (!entity) return true;

    // Frequency check - important to respect update frequency
    if (!isWithinUpdateFrequency(entity)) return false;

    // High priority behaviors still need to respect frequency checks
    // but will have more frequent updates than lower priority behaviors

    // Find the player entity
    Entity* player = findPlayerEntity();

    // If no player was found, fall back to distance from origin
    if (!player) {
        Vector2D position = entity->getPosition();
        float distSq = position.lengthSquared();

        // Use priority-based update frequency for fallback
        float priorityMultiplier = (m_priority + 1) / 10.0f;

        // Get the frame counter for this entity
        int& frameCounter = m_entityFrameCounters[entity];
        
        // Determine update frequency based on distance
        int requiredFrames;
        
        if (distSq < m_maxUpdateDistance * m_maxUpdateDistance * priorityMultiplier) {
            requiredFrames = 1; // Every frame for close entities
        } else if (distSq < m_mediumUpdateDistance * m_mediumUpdateDistance * priorityMultiplier) {
            requiredFrames = 15; // Every 15 frames for medium distance
        } else if (distSq < m_minUpdateDistance * m_minUpdateDistance * priorityMultiplier) {
            requiredFrames = 30; // Every 30 frames for far distance
        } else {
            requiredFrames = 60; // Every 60 frames for very distant entities
        }
        
        return (frameCounter >= requiredFrames);
    }

    // This section runs when player is found

    // Calculate squared distance to player (more efficient than using length())
    Vector2D toPlayer = player->getPosition() - entity->getPosition();
    float distSq = toPlayer.lengthSquared();

    // Determine update frequency based on distance to player and priority
    float priorityMultiplier = (m_priority + 1) / 10.0f; // Convert 0-9 priority to 0.1-1.0 multiplier

    // Get the frame counter for this entity
    int& frameCounter = m_entityFrameCounters[entity];
    
    // Determine update frequency based on distance from player
    int requiredFrames;
    
    if (distSq < m_maxUpdateDistance * m_maxUpdateDistance * priorityMultiplier) {
        requiredFrames = 1; // Every frame for close entities
    } else if (distSq < m_mediumUpdateDistance * m_mediumUpdateDistance * priorityMultiplier) {
        requiredFrames = 15; // Every 15 frames for medium distance
    } else if (distSq < m_minUpdateDistance * m_minUpdateDistance * priorityMultiplier) {
        requiredFrames = 30; // Every 30 frames for far distance
    } else {
        requiredFrames = 60; // Every 60 frames for very distant entities
    }
    
    return (frameCounter >= requiredFrames);
}
