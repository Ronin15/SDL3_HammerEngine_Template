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
 *    auto wanderBehavior = std::make_unique<WanderBehavior>();
 *    AIManager::Instance().registerBehavior("Wander", std::move(wanderBehavior));
 * 
 * 2. Assign behavior to entity:
 *    AIManager::Instance().assignBehaviorToEntity(npc, "Wander");
 * 
 * 3. Send message to entity's behavior:
 *    AIManager::Instance().sendMessageToEntity(npc, "pause");
 */

#include <memory>
#include <string>
#include <unordered_map>
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
     * @brief Clean up resources used by the AI Manager
     */
    void clean();
    
    // AI behavior management
    /**
     * @brief Register a new behavior for use with entities
     * @param behaviorName Unique identifier for the behavior
     * @param behavior Unique pointer to the behavior implementation
     */
    void registerBehavior(const std::string& behaviorName, std::unique_ptr<AIBehavior> behavior);
    
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
     * @brief Remove AI behavior from an entity
     * @param entity Pointer to the entity
     */
    void unassignBehaviorFromEntity(Entity* entity);
    
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
     */
    void sendMessageToEntity(Entity* entity, const std::string& message);
    
    /**
     * @brief Send a message to all entity behaviors
     * @param message Message string to broadcast
     */
    void broadcastMessage(const std::string& message);
    
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
    std::unordered_map<std::string, std::unique_ptr<AIBehavior>> m_behaviors;
    std::unordered_map<Entity*, std::string> m_entityBehaviors;
    
    // Multithreading support
    bool m_initialized{false};
    bool m_useThreading{true}; // Controls whether updates run in parallel
};

#endif // AI_MANAGER_HPP