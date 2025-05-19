/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/AIManager.hpp"
#include "ai/AIBehavior.hpp"
#include "utils/ThreadSystem.hpp"
#include <SDL3/SDL.h>
#include <vector>
#include <future>
#include <iostream>
//#include <iomanip>

bool AIManager::init() {
    if (m_initialized) {
        return true;  // Already initialized
    }

    // Disable threading temporarily for stability
    // Check if threading is available
    m_useThreading = Forge::ThreadSystem::Instance().getThreadCount() > 0;

    // Log initialization
    std::cout << "Forge Game Engine - AI Manager initialized. Threading: " << (m_useThreading ? "Enabled" : "Disabled") << "\n";

    m_initialized = true;
    return true;
}

void AIManager::update() {
    if (!m_initialized) {
        return;
    }

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
                   //std::cout << "[AI Update] Entity at (" << std::fixed << std::setprecision(2) << entity->getPosition().getX() << "," << entity->getPosition().getY() << ") running behavior: " << behaviorName << std::endl;
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
                std::cerr << "Forge Game Engine - [AI Manager] Exception in AI update task: " << e.what() << std::endl;
            }
        }
    } else {
        // Single-threaded update
        for (const auto& [entity, behaviorName] : entityBehaviorsCopy) {
            if (!entity) continue;

            AIBehavior* behavior = getBehavior(behaviorName);
            if (behavior && behavior->isActive()) {
                //std::cout << "[AI Update] Entity at (" << std::fixed << std::setprecision(2) << entity->getPosition().getX() << "," << entity->getPosition().getY() << ") running behavior: " << behaviorName << std::endl;
                behavior->update(entity);
            }
        }
    }
}

void AIManager::resetBehaviors() {
    if (!m_initialized) return;

    // Clean up each behavior for each entity
    for (const auto& [entity, behaviorName] : m_entityBehaviors) {
        if (!entity) continue;

        AIBehavior* behavior = getBehavior(behaviorName);
        if (behavior) {
            std::cout << "[AI Reset] Cleaning behavior '" << behaviorName
                      << "' for entity at position (" << entity->getPosition().getX()
                      << "," << entity->getPosition().getY() << ")" << "\n";
            behavior->clean(entity);
        }
    }

    // Clear collections
    m_entityBehaviors.clear();
    m_behaviors.clear();

    std::cout << "Forge Game Engine - [AI Manager] behaviors reset\n";
}

void AIManager::clean() {
    if (!m_initialized) return;

    // First reset all behaviors
    resetBehaviors();

    // Then perform complete shutdown operations
    m_initialized = false;
    m_useThreading = false;

    std::cout << "Forge Game Engine - AIManager completely shut down\n";
}

void AIManager::registerBehavior(const std::string& behaviorName, std::unique_ptr<AIBehavior> behavior) {
    if (!behavior) {
        std::cerr << "Forge Game Engine - [AI Manager] Attempted to register null behavior: " << behaviorName << std::endl;
        return;
    }

    // Check if behavior already exists
    if (m_behaviors.find(behaviorName) != m_behaviors.end()) {
        std::cout << "Forge Game Engine - [AI Manager] Behavior already registered: " << behaviorName << ". Replacing." << "\n";
    }

    // Store the behavior
    m_behaviors[behaviorName] = std::move(behavior);
    std::cout << "Forge Game Engine - [AI Manager] Behavior registered: " << behaviorName << "\n";
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
        std::cerr << "Forge Game Engine - [AI Manager] Attempted to assign behavior to null entity" << std::endl;
        return;
    }

    if (!hasBehavior(behaviorName)) {
        std::cerr << "Forge Game Engine - [AI Manager] Behavior not found: " << behaviorName << std::endl;
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
        std::cout << "Forge Game Engine - [AI Init] Initializing behavior '" << behaviorName
                  << "' for entity at position (" << entity->getPosition().getX()
                  << "," << entity->getPosition().getY() << ")" << std::endl;
        behavior->init(entity);
    }

    std::cout << "Forge Game Engine - [AI Manager] Behavior '" << behaviorName << "' assigned to entity\n";
}

void AIManager::unassignBehaviorFromEntity(Entity* entity) {
    if (!entity) {
        std::cerr << "Forge Game Engine - [AI Manager] Attempted to unassign behavior from null entity" << std::endl;
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
        std::cout << "Forge Game Engine - Behavior unassigned from entity\n";
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
            std::cout << "Forge Game Engine - [AI Message] Sending message to entity at ("
                      << entity->getPosition().getX() << "," << entity->getPosition().getY()
                      << ") with behavior '" << it->second << "': " << message << std::endl;
            behavior->onMessage(entity, message);
        }
    }
}

void AIManager::broadcastMessage(const std::string& message) {
    std::cout << "Forge Game Engine - [AI Broadcast] Broadcasting message to all entities: " << message << std::endl;

    int entityCount = 0;
    for (const auto& [entity, behaviorName] : m_entityBehaviors) {
        if (!entity) continue;

        AIBehavior* behavior = getBehavior(behaviorName);
        if (behavior) {
            std::cout << "Forge Game Engine - [AI Broadcast] Entity at (" << entity->getPosition().getX()
                      << "," << entity->getPosition().getY() << ") with behavior '"
                      << behaviorName << "' receiving broadcast" << std::endl;
            behavior->onMessage(entity, message);
            entityCount++;
        }
    }

    std::cout << "[AI Broadcast] Message delivered to " << entityCount << " entities" << std::endl;
}
