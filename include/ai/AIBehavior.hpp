/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef AI_BEHAVIOR_HPP
#define AI_BEHAVIOR_HPP

#include "entities/Entity.hpp"
#include <string>
#include <unordered_map>
#include <mutex>

class AIBehavior {
public:
    virtual ~AIBehavior() = default;

    // Core behavior methods
    virtual void update(EntityPtr entity) = 0;
    virtual void init(EntityPtr entity) = 0;
    virtual void clean(EntityPtr entity) = 0;

    // Behavior identification
    virtual std::string getName() const = 0;

    // Optional message handling for behavior communication
    virtual void onMessage([[maybe_unused]] EntityPtr entity, [[maybe_unused]] const std::string& message) { }

    // Behavior state access
    virtual bool isActive() const { return m_active; }
    virtual void setActive(bool active) { m_active = active; }

    // Priority handling for behavior selection
    virtual int getPriority() const { return m_priority; }
    virtual void setPriority(int priority) { m_priority = priority; }

    // Early exit condition checks
    virtual bool shouldUpdate(EntityPtr entity) const;
    virtual bool isEntityInRange([[maybe_unused]] EntityPtr entity) const { return true; }
    virtual bool isWithinUpdateFrequency(EntityPtr entity) const;

    // Entity cleanup
    virtual void cleanupEntity(EntityPtr entity);

    // Clear all frame counters (primarily for testing/benchmarking)
    virtual void clearFrameCounters() {
        std::lock_guard<std::mutex> lock(m_frameCounterMutex);
        m_entityFrameCounters.clear();
    }

    // Thread-safe access to frame counters
    void incrementFrameCounter(EntityPtr entity) const {
        if (!entity) return;
        std::lock_guard<std::mutex> lock(m_frameCounterMutex);
        m_entityFrameCounters[entity]++;
    }

    int getFrameCounter(EntityPtr entity) const {
        if (!entity) return 0;
        std::lock_guard<std::mutex> lock(m_frameCounterMutex);
        auto it = m_entityFrameCounters.find(entity);
        return (it != m_entityFrameCounters.end()) ? it->second : 0;
    }

    void resetFrameCounter(EntityPtr entity) const {
        if (!entity) return;
        std::lock_guard<std::mutex> lock(m_frameCounterMutex);
        m_entityFrameCounters[entity] = 0;
    }

    void setFrameCounter(EntityPtr entity, int value) const {
        if (!entity) return;
        std::lock_guard<std::mutex> lock(m_frameCounterMutex);
        m_entityFrameCounters[entity] = value;
    }

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

    // Per-entity frame counters protected by mutex for thread safety
    mutable std::unordered_map<EntityPtr, int, std::hash<EntityPtr>> m_entityFrameCounters{};
    mutable std::mutex m_frameCounterMutex{};

    // Distance-based update parameters
    float m_maxUpdateDistance{8000.0f}; // Maximum distance at which entity is updated every frame
    float m_mediumUpdateDistance{10000.0f}; // Medium distance - entity updated less frequently
    float m_minUpdateDistance{25000.0f}; // Minimum distance - entity updated rarely

    // Helper method to find the player entity in the current game state
    // Implementation varies:
    // - In tests: returns nullptr to use fallback distance-from-origin logic
    // - In game: returns player entity from active game state
    // This approach enables unit testing without dependencies on GameState classes
    static EntityPtr findPlayerEntity();
};

#endif // AI_BEHAVIOR_HPP
