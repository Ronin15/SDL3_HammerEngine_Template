/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "AIManager.hpp"
#include "AIBehavior.hpp"
#include "ThreadSystem.hpp"
#include <SDL3/SDL.h>
#include <vector>
#include <future>

bool AIManager::init() {
    if (m_initialized) {
        return true;  // Already initialized
    }
    
    // Disable threading temporarily for stability
    // Check if threading is available
    m_useThreading = Forge::ThreadSystem::Instance().getThreadCount() > 0;
    
    // Log initialization
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "AIManager initialized. Threading: %s", 
                m_useThreading ? "Enabled" : "Disabled");
    
    m_initialized = true;
    return true;
}

void AIManager::update() {
    if (!m_initialized) return;
    
    // Make a copy of the entity-behavior map to avoid issues if the map changes during iteration
    auto entityBehaviorsCopy = m_entityBehaviors;
    
    // If threading is enabled, distribute AI updates across worker threads
    if (m_useThreading && entityBehaviorsCopy.size() > 1) {
        // Reserve enough capacity for all entities
        Forge::ThreadSystem::Instance().reserveQueueCapacity(entityBehaviorsCopy.size());
        
        // Store futures to track task completion
        std::vector<std::future<void>> taskFutures;
        taskFutures.reserve(entityBehaviorsCopy.size());
        
        // Create a task for each entity
        for (const auto& [entity, behaviorName] : entityBehaviorsCopy) {
            // Capture by value since we're using a copy of the map
            auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult([this, entity, behaviorName]() -> void {
                if (!entity) return;
                
                // Get the behavior and update the entity
                AIBehavior* behavior = this->getBehavior(behaviorName);
                if (behavior && behavior->isActive()) {
                    behavior->update(entity);
                }
            });
            
            taskFutures.push_back(std::move(future));
        }
        
        // Wait for all tasks to complete
        for (auto& future : taskFutures) {
            try {
                future.get();
            } catch (const std::exception& e) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Exception in AI update task: %s", e.what());
            }
        }
    } else {
        // Single-threaded update
        for (const auto& [entity, behaviorName] : entityBehaviorsCopy) {
            if (!entity) continue;
            
            AIBehavior* behavior = getBehavior(behaviorName);
            if (behavior && behavior->isActive()) {
                behavior->update(entity);
            }
        }
    }
}

void AIManager::clean() {
    if (!m_initialized) return;
    
    // Clean up each behavior for each entity
    for (const auto& [entity, behaviorName] : m_entityBehaviors) {
        if (!entity) continue;
        
        AIBehavior* behavior = getBehavior(behaviorName);
        if (behavior) {
            behavior->clean(entity);
        }
    }
    
    // Clear collections
    m_entityBehaviors.clear();
    m_behaviors.clear();
    
    m_initialized = false;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "AIManager cleaned up");
}

void AIManager::registerBehavior(const std::string& behaviorName, std::unique_ptr<AIBehavior> behavior) {
    if (!behavior) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Attempted to register null behavior: %s", behaviorName.c_str());
        return;
    }
    
    // Check if behavior already exists
    if (m_behaviors.find(behaviorName) != m_behaviors.end()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Behavior already registered: %s. Replacing.", behaviorName.c_str());
    }
    
    // Store the behavior
    m_behaviors[behaviorName] = std::move(behavior);
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Behavior registered: %s", behaviorName.c_str());
}

bool AIManager::hasBehavior(const std::string& behaviorName) const {
    return m_behaviors.find(behaviorName) != m_behaviors.end();
}

AIBehavior* AIManager::getBehavior(const std::string& behaviorName) const {
    auto it = m_behaviors.find(behaviorName);
    if (it != m_behaviors.end()) {
        return it->second.get();
    }
    return nullptr;
}

void AIManager::assignBehaviorToEntity(Entity* entity, const std::string& behaviorName) {
    if (!entity) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Attempted to assign behavior to null entity");
        return;
    }
    
    if (!hasBehavior(behaviorName)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Behavior not found: %s", behaviorName.c_str());
        return;
    }
    
    // If entity already has a behavior, clean it up first
    if (entityHasBehavior(entity)) {
        unassignBehaviorFromEntity(entity);
    }
    
    // Assign new behavior
    m_entityBehaviors[entity] = behaviorName;
    
    // Initialize the behavior for this entity
    AIBehavior* behavior = getBehavior(behaviorName);
    if (behavior) {
        behavior->init(entity);
    }
    
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Behavior '%s' assigned to entity", behaviorName.c_str());
}

void AIManager::unassignBehaviorFromEntity(Entity* entity) {
    if (!entity) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Attempted to unassign behavior from null entity");
        return;
    }
    
    auto it = m_entityBehaviors.find(entity);
    if (it != m_entityBehaviors.end()) {
        // Clean up the behavior
        AIBehavior* behavior = getBehavior(it->second);
        if (behavior) {
            behavior->clean(entity);
        }
        
        // Remove from map
        m_entityBehaviors.erase(it);
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Behavior unassigned from entity");
    }
}

bool AIManager::entityHasBehavior(Entity* entity) const {
    if (!entity) return false;
    return m_entityBehaviors.find(entity) != m_entityBehaviors.end();
}

void AIManager::sendMessageToEntity(Entity* entity, const std::string& message) {
    if (!entity) return;
    
    auto it = m_entityBehaviors.find(entity);
    if (it != m_entityBehaviors.end()) {
        AIBehavior* behavior = getBehavior(it->second);
        if (behavior) {
            behavior->onMessage(entity, message);
        }
    }
}

void AIManager::broadcastMessage(const std::string& message) {
    for (const auto& [entity, behaviorName] : m_entityBehaviors) {
        if (!entity) continue;
        
        AIBehavior* behavior = getBehavior(behaviorName);
        if (behavior) {
            behavior->onMessage(entity, message);
        }
    }
}