/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef AI_BEHAVIOR_HPP
#define AI_BEHAVIOR_HPP

#include "entities/Entity.hpp"
#include <string>
#include <unordered_map>

class AIBehavior {
public:
    virtual ~AIBehavior() = default;

    // Core behavior methods
    virtual void update(Entity* entity) = 0;
    virtual void init(Entity* entity) = 0;
    virtual void clean(Entity* entity) = 0;

    // Behavior identification
    virtual std::string getName() const = 0;

    // Optional message handling for behavior communication
    virtual void onMessage([[maybe_unused]] Entity* entity, [[maybe_unused]] const std::string& message) { }

    // Behavior state access
    virtual bool isActive() const { return m_active; }
    virtual void setActive(bool active) { m_active = active; }

    // Priority handling for behavior selection
    virtual int getPriority() const { return m_priority; }
    virtual void setPriority(int priority) { m_priority = priority; }

    // Early exit condition checks
    virtual bool shouldUpdate(Entity* entity) const;
    virtual bool isEntityInRange([[maybe_unused]] Entity* entity) const { return true; }
    virtual bool isWithinUpdateFrequency(Entity* entity) const;

    // Entity cleanup
    virtual void cleanupEntity(Entity* entity);
    
    // Clear all frame counters (primarily for testing/benchmarking)
    virtual void clearFrameCounters() { m_entityFrameCounters.clear(); }

    // Update frequency control
    virtual void setUpdateFrequency(int framesPerUpdate) { m_updateFrequency = framesPerUpdate; }
    virtual int getUpdateFrequency() const { return m_updateFrequency; }

    // Distance-based update control
    virtual void setMaxUpdateDistance(float distance) { m_maxUpdateDistance = distance; }
    virtual float getMaxUpdateDistance() const { return m_maxUpdateDistance; }

    virtual void setMediumUpdateDistance(float distance) { m_mediumUpdateDistance = distance; }
    virtual float getMediumUpdateDistance() const { return m_mediumUpdateDistance; }

    virtual void setMinUpdateDistance(float distance) { m_minUpdateDistance = distance; }
    virtual float getMinUpdateDistance() const { return m_minUpdateDistance; }

    // Set all distance parameters at once
    virtual void setUpdateDistances(float maxDist, float mediumDist, float minDist) {
        m_maxUpdateDistance = maxDist;
        m_mediumUpdateDistance = mediumDist;
        m_minUpdateDistance = minDist;
    }

    // Expose frame counter to AIManager
    friend class AIManager;

protected:
    bool m_active{true};
    int m_priority{0};  // Higher values = higher priority
    int m_updateFrequency{1}; // How often to update (1 = every frame, 2 = every other frame, etc.)

    // Per-entity frame counters
    mutable std::unordered_map<Entity*, int> m_entityFrameCounters{};

    // Distance-based update parameters
    float m_maxUpdateDistance{10000.0f}; // Maximum distance at which entity is updated every frame
    float m_mediumUpdateDistance{15000.0f}; // Medium distance - entity updated less frequently
    float m_minUpdateDistance{20000.0f}; // Minimum distance - entity updated rarely

    // Helper method to find the player entity in the current game state
    // Implementation varies:
    // - In tests: returns nullptr to use fallback distance-from-origin logic
    // - In game: returns player entity from active game state
    // This approach enables unit testing without dependencies on GameState classes
    Entity* findPlayerEntity() const;
};

#endif // AI_BEHAVIOR_HPP
