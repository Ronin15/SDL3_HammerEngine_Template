/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/AIManager.hpp"
#include "ai/AIBehavior.hpp"
#include "core/ThreadSystem.hpp"
#include <SDL3/SDL.h>
#include <vector>
#include <future>
#include <iostream>
//#include <iomanip>

void AIManager::ensureOptimizationCachesValid() {
    if (!m_cacheValid) {
        rebuildEntityBehaviorCache();
    }

    if (!m_batchesValid) {
        rebuildBehaviorBatches();
    }
}

void AIManager::rebuildEntityBehaviorCache() {
    // Clear existing cache
    m_entityBehaviorCache.clear();
    m_entityBehaviorCache.reserve(m_entityBehaviors.size());

    // Populate cache with entity-behavior pairs
    for (const auto& [entity, behaviorName] : m_entityBehaviors) {
        if (!entity) continue;
    
        AIBehavior* behavior = getBehavior(behaviorName);
        if (behavior) {
            m_entityBehaviorCache.push_back({entity, behavior, behaviorName});
        }
    }

    m_cacheValid = true;
}

void AIManager::rebuildBehaviorBatches() {
    // Clear existing batches
    m_behaviorBatches.clear();

    // Group entities by behavior
    for (const auto& [entity, behaviorName] : m_entityBehaviors) {
        if (!entity) continue;
    
        // Add entity to appropriate batch
        m_behaviorBatches[behaviorName].push_back(entity);
    }

    m_batchesValid = true;
}

void AIManager::invalidateOptimizationCaches() {
    m_cacheValid = false;
    m_batchesValid = false;
}

void AIManager::updateBehaviorBatch(const std::string& behaviorName, const BehaviorBatch& batch) {
    AIBehavior* behavior = getBehavior(behaviorName);
    if (!behavior || !behavior->isActive() || batch.empty()) {
        return;
    }
    
    // Update frame counter for behavior
    behavior->m_framesSinceLastUpdate++;
    
    // Apply early exit condition: only update at specified frequency
    if (!behavior->isWithinUpdateFrequency()) {
        return;
    }
    
    // Collect entities that pass the early exit checks
    std::vector<Entity*> entitiesToUpdate;
    entitiesToUpdate.reserve(batch.size());
    
    for (Entity* entity : batch) {
        if (!entity) continue;
        
        // Apply early exit conditions
        if (!behavior->shouldUpdate(entity) || !behavior->isEntityInRange(entity)) {
            continue;
        }
        
        entitiesToUpdate.push_back(entity);
    }
    
    // Reset frame counter after update
    if (behavior->isWithinUpdateFrequency()) {
        behavior->m_framesSinceLastUpdate = 0;
    }
    
    // Skip processing if no entities passed the early exit checks
    if (entitiesToUpdate.empty()) {
        return;
    }

    if (m_useThreading && entitiesToUpdate.size() > 1) {
        // Multithreaded batch processing
        std::vector<std::future<void>> taskFutures;
        taskFutures.reserve(entitiesToUpdate.size());

        // Reserve capacity in thread system
        Forge::ThreadSystem::Instance().reserveQueueCapacity(entitiesToUpdate.size());

        for (Entity* entity : entitiesToUpdate) {
            auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult(
                [behavior, entity]() -> void {
                    behavior->update(entity);
                });
    
            taskFutures.push_back(std::move(future));
        }

        // Wait for all tasks to complete
        for (auto& future : taskFutures) {
            try {
                future.get();
            } catch (const std::exception& e) {
                std::cerr << "Forge Game Engine - [AI Manager] Exception in batch processing: " << e.what() << std::endl;
            }
        }
    } else {
        // Single-threaded batch processing
        for (Entity* entity : entitiesToUpdate) {
            behavior->update(entity);
        }
    }
}

void AIManager::batchProcessEntities(const std::string& behaviorName, const std::vector<Entity*>& entities) {
    AIBehavior* behavior = getBehavior(behaviorName);
    if (!behavior || !behavior->isActive()) {
        return;
    }
    
    // Update frame counter for behavior
    behavior->m_framesSinceLastUpdate++;
    
    // Apply early exit condition: only update at specified frequency
    if (!behavior->isWithinUpdateFrequency()) {
        return;
    }
    
    // Collect entities that pass the early exit checks
    std::vector<Entity*> entitiesToUpdate;
    entitiesToUpdate.reserve(entities.size());
    
    for (Entity* entity : entities) {
        if (!entity) continue;
        
        // Apply early exit conditions
        if (!behavior->shouldUpdate(entity) || !behavior->isEntityInRange(entity)) {
            continue;
        }
        
        entitiesToUpdate.push_back(entity);
    }
    
    // Reset frame counter after update
    if (behavior->isWithinUpdateFrequency()) {
        behavior->m_framesSinceLastUpdate = 0;
    }
    
    // Skip processing if no entities passed the early exit checks
    if (entitiesToUpdate.empty()) {
        return;
    }

    if (m_useThreading && entitiesToUpdate.size() > 1) {
        // Multithreaded batch processing
        std::vector<std::future<void>> taskFutures;
        taskFutures.reserve(entitiesToUpdate.size());

        // Reserve capacity in thread system
        Forge::ThreadSystem::Instance().reserveQueueCapacity(entitiesToUpdate.size());

        for (Entity* entity : entitiesToUpdate) {
            auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult(
                [behavior, entity]() -> void {
                    behavior->update(entity);
                });
    
            taskFutures.push_back(std::move(future));
        }

        // Wait for all tasks to complete
        for (auto& future : taskFutures) {
            try {
                future.get();
            } catch (const std::exception& e) {
                std::cerr << "Forge Game Engine - [AI Manager] Exception in batch processing: " << e.what() << std::endl;
            }
        }
    } else {
        // Single-threaded batch processing
        for (Entity* entity : entitiesToUpdate) {
            behavior->update(entity);
        }
    }
}

bool AIManager::init() {
    if (m_initialized) {
        return true;  // Already initialized
    }

    // Check if threading is available
    m_useThreading = Forge::ThreadSystem::Instance().getThreadCount() > 0;

    // Log initialization
    std::cout << "Forge Game Engine - AI Manager initialized!\n";
    std::cout << "Forge Game Engine - AI Manager Threading: " << (m_useThreading ? "Enabled!" : "Disabled?") << "\n";

    m_initialized = true;
    return true;
}

void AIManager::update() {
    if (!m_initialized) {
        return;
    }

    // Process any queued messages before updating behaviors
    processMessageQueue();

    // We now use the optimized batch update method
    batchUpdateAllBehaviors();
}

void AIManager::batchUpdateAllBehaviors() {
    if (!m_initialized) {
        return;
    }
    
    // Ensure our optimization caches are valid
    ensureOptimizationCachesValid();

    // If threading is enabled, distribute AI updates across worker threads
    if (m_useThreading && m_behaviorBatches.size() > 0) {
        // Store futures to track task completion
        std::vector<std::future<void>> taskFutures;
        size_t totalEntities = 0;
        
        // Count total entities to reserve capacity
        for (const auto& [behaviorName, batch] : m_behaviorBatches) {
            totalEntities += batch.size();
        }
        
        // Reserve enough capacity for all entities
        Forge::ThreadSystem::Instance().reserveQueueCapacity(totalEntities);
        taskFutures.reserve(totalEntities);

        // Process each behavior batch
        for (const auto& [behaviorName, batch] : m_behaviorBatches) {
            if (batch.empty()) continue;
            
            // Update this batch of entities
            updateBehaviorBatch(behaviorName, batch);
        }
    } else {
        // Single-threaded update using cached behavior references for speed
        for (const auto& cache : m_entityBehaviorCache) {
            if (!cache.entity) continue;
            if (cache.behavior && cache.behavior->isActive()) {
                cache.behavior->update(cache.entity);
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
    
    // Clear optimization caches
    m_entityBehaviorCache.clear();
    m_behaviorBatches.clear();
    m_cacheValid = false;
    m_batchesValid = false;

    std::cout << "Forge Game Engine - [AI Manager] behaviors reset\n";
}

void AIManager::clean() {
    if (!m_initialized) return;

    // Process any remaining messages
    processMessageQueue();

    // First reset all behaviors
    resetBehaviors();

    // Then perform complete shutdown operations
    m_initialized = false;
    m_useThreading = false;
    
    // Make sure caches are cleared
    m_entityBehaviorCache.clear();
    m_behaviorBatches.clear();
    m_cacheValid = false;
    m_batchesValid = false;
    
    // Clear message queue
    {
        std::lock_guard<std::mutex> lock(m_messageQueueMutex);
        std::queue<QueuedMessage> empty;
        std::swap(m_messageQueue, empty);
    }

    std::cout << "Forge Game Engine - AIManager completely shut down\n";
}

void AIManager::processMessageQueue() {
    if (!m_initialized || m_processingMessages) {
        return;
    }

    // Set flag to prevent recursive calls during message processing
    m_processingMessages = true;

    std::queue<QueuedMessage> localQueue;

    // Safely get all queued messages
    {
        std::lock_guard<std::mutex> lock(m_messageQueueMutex);
        std::swap(localQueue, m_messageQueue);
    }

    // Process all queued messages
    int processedCount = 0;
    while (!localQueue.empty()) {
        const QueuedMessage& msg = localQueue.front();
    
        if (msg.targetEntity == nullptr) {
            // This is a broadcast message
            deliverBroadcastMessage(msg.message);
        } else {
            // This is a targeted message
            deliverMessageToEntity(msg.targetEntity, msg.message);
        }
    
        localQueue.pop();
        processedCount++;
    }

    if (processedCount > 0) {
        std::cout << "Forge Game Engine - [AI Message Queue] Processed " << processedCount << " messages" << std::endl;
    }

    // Reset processing flag
    m_processingMessages = false;
}

void AIManager::deliverMessageToEntity(Entity* entity, const std::string& message) {
    if (!entity) return;

    auto it = m_entityBehaviors.find(entity);
    if (it != m_entityBehaviors.end()) {
        AIBehavior* behavior = getBehavior(it->second);
        if (behavior) {
            std::cout << "Forge Game Engine - [AI Message] Delivering message to entity at ("
                    << entity->getPosition().getX() << "," << entity->getPosition().getY()
                    << ") with behavior '" << it->second << "': " << message << std::endl;
            behavior->onMessage(entity, message);
        }
    }
}

void AIManager::deliverBroadcastMessage(const std::string& message) {
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

// Implementation moved to the end of the file

void AIManager::registerBehavior(const std::string& behaviorName, std::shared_ptr<AIBehavior> behavior) {
    if (!behavior) {
        std::cerr << "Forge Game Engine - [AI Manager] Attempted to register null behavior: " << behaviorName << std::endl;
        return;
    }

    // Check if behavior already exists
    if (m_behaviors.find(behaviorName) != m_behaviors.end()) {
        std::cout << "Forge Game Engine - [AI Manager] Behavior already registered: " << behaviorName << ". Replacing." << "\n";
    }

    // Store the behavior
    m_behaviors[behaviorName] = behavior;
    
    // Invalidate caches since behavior collection changed
    invalidateOptimizationCaches();
    
    std::cout << "Forge Game Engine - [AI Manager] Behavior registered: " << behaviorName << "\n";
}

inline bool AIManager::hasBehavior(const std::string& behaviorName) const {
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
    
    // Invalidate caches since entity-behavior mapping changed
    invalidateOptimizationCaches();

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
        
        // Invalidate caches since entity-behavior mapping changed
        invalidateOptimizationCaches();
        
        std::cout << "Forge Game Engine - Behavior unassigned from entity\n";
    }
}

inline bool AIManager::entityHasBehavior(Entity* entity) const {
    if (!entity) return false;
    return m_entityBehaviors.find(entity) != m_entityBehaviors.end();
}

void AIManager::sendMessageToEntity(Entity* entity, const std::string& message, bool immediate) {
    if (!entity) return;

    if (immediate) {
        deliverMessageToEntity(entity, message);
    } else {
        // Queue the message for later processing
        QueuedMessage queuedMsg;
        queuedMsg.targetEntity = entity;
        queuedMsg.message = message;
        queuedMsg.timestamp = SDL_GetTicks();

        std::lock_guard<std::mutex> lock(m_messageQueueMutex);
        m_messageQueue.push(queuedMsg);
        
        std::cout << "Forge Game Engine - [AI Message] Queued message for entity at ("
                  << entity->getPosition().getX() << "," << entity->getPosition().getY()
                  << "): " << message << std::endl;
    }
}

void AIManager::broadcastMessage(const std::string& message, bool immediate) {
    if (immediate) {
        deliverBroadcastMessage(message);
    } else {
        // Queue broadcast message
        QueuedMessage queuedMsg;
        queuedMsg.targetEntity = nullptr;  // nullptr indicates broadcast
        queuedMsg.message = message;
        queuedMsg.timestamp = SDL_GetTicks();

        std::lock_guard<std::mutex> lock(m_messageQueueMutex);
        m_messageQueue.push(queuedMsg);
        
        std::cout << "Forge Game Engine - [AI Broadcast] Queued broadcast message: " << message << std::endl;
    }
}