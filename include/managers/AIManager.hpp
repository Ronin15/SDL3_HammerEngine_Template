/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef AI_MANAGER_HPP
#define AI_MANAGER_HPP

/**
 * @file AIManager.hpp
 * @brief Central manager for AI behaviors and entity AI assignments
 *
 * The AIManager provides a centralized system for:
 * - Registering reusable AI behaviors
 * - Assigning behaviors to game entities
 * - Updating all AI-controlled entities efficiently
 * - Communicating with AI behaviors via messages
 *
 * Usage example:
 *
 * 1. Register behaviors:
 *    auto wanderBehavior = std::make_shared<WanderBehavior>();
 *    AIManager::Instance().registerBehavior("Wander", wanderBehavior);
 *
 * 2. Assign behavior to entity:
 *    AIManager::Instance().assignBehaviorToEntity(npc, "Wander");
 *
 * 3. Send message to entity's behavior:
 *    AIManager::Instance().sendMessageToEntity(npc, "pause");
 */

#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <boost/container/flat_map.hpp>
#include "entities/Entity.hpp"
#include "ai/AIBehavior.hpp"

// AIBehavior is now fully included, not just forward declared

class AIManager {
public:
    /**
     * @brief Get the singleton instance of AIManager
     * @return Reference to the AIManager instance
     */
    static AIManager& Instance() {
        static AIManager instance;
        return instance;
    }

    /**
     * @brief Initialize the AI Manager
     * @return True if initialization succeeded, false otherwise
     */
    bool init();

    /**
     * @brief Update all AI-controlled entities
     *
     * This method is automatically called by the game engine
     * and updates all entities with assigned AI behaviors.
     * Updates can run in parallel using the ThreadSystem if available.
     */
    void update();

    /**
     * @brief Reset all AI behaviors without shutting down the manager
     *
     * This method clears all registered behaviors and entity assignments
     * but keeps the manager initialized. Use this when changing game states
     * or scenes while the game is still running.
     */
    void resetBehaviors();

    /**
     * @brief Clean up resources used by the AI Manager
     */
    void clean();

    // AI behavior management
    /**
     * @brief Register a new behavior for use with entities
     * @param behaviorName Unique identifier for the behavior
     * @param behavior Shared pointer to the behavior implementation
     */
    void registerBehavior(const std::string& behaviorName, std::shared_ptr<AIBehavior> behavior);

    /**
     * @brief Check if a behavior exists
     * @param behaviorName Name of the behavior to check
     * @return True if the behavior exists, false otherwise
     */
    bool hasBehavior(const std::string& behaviorName) const;

    /**
     * @brief Get a pointer to a behavior
     * @param behaviorName Name of the behavior to retrieve
     * @return Pointer to the behavior, or nullptr if not found
     */
    AIBehavior* getBehavior(const std::string& behaviorName) const;

    // Entity-behavior assignment
    /**
     * @brief Assign an AI behavior to an entity
     * @param entity Pointer to the entity
     * @param behaviorName Name of the behavior to assign
     */
    void assignBehaviorToEntity(Entity* entity, const std::string& behaviorName);
    
    /**
     * @brief Process multiple entities with the same behavior type in batches
     * @param behaviorName The behavior to process
     * @param entities Vector of entities to process
     */
    void batchProcessEntities(const std::string& behaviorName, const std::vector<Entity*>& entities);

    /**
     * @brief Process all behaviors in batches for maximum performance
     * This is the most optimized way to update all AI entities.
     * It will automatically use threading if available.
     */
    void batchUpdateAllBehaviors();

    /**
     * @brief Remove AI behavior from an entity
     * @param entity Pointer to the entity
     */
    void unassignBehaviorFromEntity(Entity* entity);
    
    /**
     * @brief Rebuild optimization caches if they're invalid
     * This will be called automatically when needed, but can be called
     * manually if you know the caches should be refreshed
     */
    void ensureOptimizationCachesValid();

    /**
     * @brief Check if an entity has an assigned behavior
     * @param entity Pointer to the entity
     * @return True if the entity has a behavior, false otherwise
     */
    bool entityHasBehavior(Entity* entity) const;

    // Advanced features
    /**
     * @brief Send a message to a specific entity's behavior
     * @param entity Target entity
     * @param message Message string (e.g., "pause", "resume", "attack")
     * @param immediate If true, delivers immediately; if false, queues for next update
     */
    void sendMessageToEntity(Entity* entity, const std::string& message, bool immediate = false);

    /**
     * @brief Send a message to all entity behaviors
     * @param message Message string to broadcast
     * @param immediate If true, delivers immediately; if false, queues for next update
     */
    void broadcastMessage(const std::string& message, bool immediate = false);
    
    /**
     * @brief Process all queued messages
     * This happens automatically during update() but can be called manually
     */
    void processMessageQueue();

    // Utility methods
    /**
     * @brief Get the number of registered behaviors
     * @return Count of behaviors
     */
    size_t getBehaviorCount() const { return m_behaviors.size(); }

    /**
     * @brief Get the number of entities with AI behaviors
     * @return Count of managed entities
     */
    size_t getManagedEntityCount() const { return m_entityBehaviors.size(); }

private:
    // Singleton constructor
    AIManager() = default;
    ~AIManager() = default;

    // Delete copy constructor and assignment operator
    AIManager(const AIManager&) = delete;
    AIManager& operator=(const AIManager&) = delete;

    // Storage for behaviors and entity assignments
    boost::container::flat_map<std::string, std::shared_ptr<AIBehavior>> m_behaviors{};
    boost::container::flat_map<Entity*, std::string> m_entityBehaviors{};
    
    // Cache for quick lookup of entity-behavior pairs (optimization)
    struct EntityBehaviorCache {
        Entity* entity;
        AIBehavior* behavior;
        std::string behaviorName;
        
        // Performance statistics
        Uint64 lastUpdateTime{0};
        float averageUpdateTimeMs{0.0f};
    };
    std::vector<EntityBehaviorCache> m_entityBehaviorCache{};
    bool m_cacheValid{false};

    // Multithreading support
    bool m_initialized{false};
    bool m_useThreading{true}; // Controls whether updates run in parallel
    
    // For batch processing optimization
    using BehaviorBatch = std::vector<Entity*>;
    boost::container::flat_map<std::string, BehaviorBatch> m_behaviorBatches{};
    bool m_batchesValid{false};
    
    // Private helper methods for optimizations
    void rebuildEntityBehaviorCache();
    void rebuildBehaviorBatches();
    void invalidateOptimizationCaches();
    void updateBehaviorBatch(const std::string& behaviorName, const BehaviorBatch& batch);
    
    // Message queue system
    struct QueuedMessage {
        Entity* targetEntity;  // nullptr for broadcast
        std::string message;
        Uint64 timestamp;
    };
    std::queue<QueuedMessage> m_messageQueue;
    std::mutex m_messageQueueMutex;
    bool m_processingMessages{false};
    
    // Message delivery helpers
    void deliverMessageToEntity(Entity* entity, const std::string& message);
    void deliverBroadcastMessage(const std::string& message);
};

#endif // AI_MANAGER_HPP
