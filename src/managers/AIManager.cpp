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
            // Using string_view to avoid copying the behavior name string
            m_entityBehaviorCache.push_back({
                entity,
                behavior,
                std::string_view(behaviorName),
                0, // lastUpdateTime
                {} // perfStats
            });
        }
    }

    m_cacheValid = true;
    AI_LOG("Entity behavior cache rebuilt with " << m_entityBehaviorCache.size() << " entries");
}

void AIManager::rebuildBehaviorBatches() {
    // Clear existing batches
    m_behaviorBatches.clear();

    // Reserve capacity based on number of behaviors (with a minimum size to reduce reallocations)
    m_behaviorBatches.reserve(std::max<size_t>(16, m_behaviors.size()));

    // Group entities by behavior
    for (const auto& [entity, behaviorName] : m_entityBehaviors) {
        if (!entity) continue;

        // Add entity to appropriate batch
        m_behaviorBatches[behaviorName].push_back(entity);
    }

    // Optimize memory usage of each batch
    for (auto& [behaviorName, batch] : m_behaviorBatches) {
        batch.shrink_to_fit();
    }

    m_batchesValid = true;
    AI_LOG("Behavior batches rebuilt with " << m_behaviorBatches.size() << " behavior types");
}

void AIManager::invalidateOptimizationCaches() {
    m_cacheValid = false;
    m_batchesValid = false;
}

void AIManager::updateBehaviorBatch(const std::string_view& behaviorName, const BehaviorBatch& batch) {
    // Look up behavior just once, using string_view for efficient comparison
    AIBehavior* behavior = nullptr;
    for (const auto& [name, behaviorPtr] : m_behaviors) {
        if (name == behaviorName) {
            behavior = behaviorPtr.get();
            break;
        }
    }

    if (!behavior || !behavior->isActive() || batch.empty()) {
        return;
    }

    // Start timing for performance tracking
    auto startTime = getCurrentTimeNanos();

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
    behavior->m_framesSinceLastUpdate = 0;

    // Skip processing if no entities passed the early exit checks
    if (entitiesToUpdate.empty()) {
        return;
    }

    // Use the common helper method to process entities
    processEntitiesWithBehavior(behavior, entitiesToUpdate, m_useThreading && entitiesToUpdate.size() > 1);

    // Record performance data
    auto endTime = getCurrentTimeNanos();
    double elapsedTimeMs = (endTime - startTime) / 1000000.0;
    recordBehaviorPerformance(behaviorName, elapsedTimeMs);
}

// New helper method to eliminate code duplication
void AIManager::processEntitiesWithBehavior(AIBehavior* behavior, const std::vector<Entity*>& entities, bool useThreading) {
    if (useThreading) {
        // Multithreaded batch processing
        std::vector<std::future<void>> taskFutures;
        taskFutures.reserve(entities.size());

        // Ensure the thread tasks have proper exception handling
        try {
            for (Entity* entity : entities) {
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
                    AI_LOG("Exception in batch processing: " << e.what());
                }
            }
        } catch (const std::exception& e) {
            AI_LOG("Fatal error in thread task submission: " << e.what());
        }
    } else {
        // Single-threaded batch processing
        for (Entity* entity : entities) {
            behavior->update(entity);
        }
    }
}

void AIManager::batchProcessEntities(const std::string& behaviorName, const std::vector<Entity*>& entities) {
    AIBehavior* behavior = getBehavior(behaviorName);
    if (!behavior || !behavior->isActive()) {
        return;
    }

    // Start timing for performance tracking
    auto startTime = getCurrentTimeNanos();

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
    behavior->m_framesSinceLastUpdate = 0;

    // Skip processing if no entities passed the early exit checks
    if (entitiesToUpdate.empty()) {
        return;
    }

    // Use the common helper method to process entities
    processEntitiesWithBehavior(behavior, entitiesToUpdate, m_useThreading && entitiesToUpdate.size() > 1);

    // Record performance data
    auto endTime = getCurrentTimeNanos();
    double elapsedTimeMs = (endTime - startTime) / 1000000.0;
    recordBehaviorPerformance(behaviorName, elapsedTimeMs);
}
// This code was replaced by the processEntitiesWithBehavior method

void AIManager::recordBehaviorPerformance(const std::string_view& behaviorName, double timeMs) {
    // Convert string_view to string for map lookup
    std::string name(behaviorName);
    m_behaviorPerformanceStats[name].addSample(timeMs);
}

bool AIManager::init() {
    if (m_initialized) {
        return true;  // Already initialized
    }

    // Check if threading is available
    m_useThreading = Forge::ThreadSystem::Instance().getThreadCount() > 0;

    // Pre-allocate message queue vectors with a reasonable default capacity
    m_incomingMessageQueue.reserve(128);
    m_processingMessageQueue.reserve(128);

    // Reset performance stats
    m_messageQueueStats.reset();
    m_behaviorPerformanceStats.clear();

    // Log initialization
    AI_LOG("AI Manager initialized!");
    AI_LOG("AI Manager Threading: " << (m_useThreading ? "Enabled!" : "Disabled?"));

    m_initialized = true;
    return true;
}

void AIManager::update() {
    if (!m_initialized) {
        return;
    }

    auto startTime = getCurrentTimeNanos();

    // Process any queued messages before updating behaviors
    processMessageQueue();

    // We now use the optimized batch update method
    batchUpdateAllBehaviors();

    auto endTime = getCurrentTimeNanos();
    double totalUpdateMs = (endTime - startTime) / 1000000.0;

    // Only log in debug builds and track for performance stats
    AI_LOG_DETAIL("Total AI update time: " << totalUpdateMs << "ms");
    m_messageQueueStats.addSample(totalUpdateMs);
}

void AIManager::batchUpdateAllBehaviors() {
    if (!m_initialized) {
        return;
    }

    // Ensure our optimization caches are valid
    ensureOptimizationCachesValid();

    auto startTime = getCurrentTimeNanos();

    // If threading is enabled, use behavior batches for parallel processing
    if (m_useThreading && !m_behaviorBatches.empty()) {
        // Process each behavior batch
        for (const auto& [behaviorName, batch] : m_behaviorBatches) {
            if (batch.empty()) continue;

            // Update this batch of entities
            updateBehaviorBatch(behaviorName, batch);
        }
    } else {
        // Single-threaded update using cached behavior references for speed
        for (const auto& cache : m_entityBehaviorCache) {
            if (!cache.entity || !cache.behavior || !cache.behavior->isActive()) continue;

            auto entityStartTime = getCurrentTimeNanos();
            cache.behavior->update(cache.entity);
            auto entityEndTime = getCurrentTimeNanos();

            double entityUpdateMs = (entityEndTime - entityStartTime) / 1000000.0;
            recordBehaviorPerformance(cache.behaviorName, entityUpdateMs);
        }
    }

    auto endTime = getCurrentTimeNanos();
    double batchUpdateMs = (endTime - startTime) / 1000000.0;

    AI_LOG_DETAIL("Batch behavior updates completed in " << batchUpdateMs << "ms");
    m_messageQueueStats.addSample(batchUpdateMs);
}

void AIManager::resetBehaviors() {
    if (!m_initialized) return;

    // Clean up each behavior for each entity
    for (const auto& [entity, behaviorName] : m_entityBehaviors) {
        if (!entity) continue;

        AIBehavior* behavior = getBehavior(behaviorName);
        if (behavior) {
            AI_LOG_DETAIL("[AI Reset] Cleaning behavior '" << behaviorName
                      << "' for entity at position (" << entity->getPosition().getX()
                      << "," << entity->getPosition().getY() << ")");
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

    // Reset performance stats
    m_behaviorPerformanceStats.clear();
    m_messageQueueStats.reset();

    AI_LOG("behaviors reset");
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

    // Clear message queues
    {
        std::lock_guard<std::mutex> lock(m_messageQueueMutex);
        m_incomingMessageQueue.clear();
        m_processingMessageQueue.clear();
    }

    AI_LOG("AIManager completely shut down");
}

void AIManager::processMessageQueue() {
    if (!m_initialized || m_processingMessages) {
        return;
    }

    auto startTime = getCurrentTimeNanos();

    // Set flag to prevent recursive calls during message processing
    m_processingMessages = true;

    // Safely get all queued messages by swapping vectors
    {
        std::lock_guard<std::mutex> lock(m_messageQueueMutex);
        m_processingMessageQueue.swap(m_incomingMessageQueue);
        m_incomingMessageQueue.clear();

        // Pre-allocate capacity for next batch based on current usage
        // (use next power of 2 for better memory allocation)
        if (!m_processingMessageQueue.empty()) {
            size_t nextSize = m_processingMessageQueue.size() * 2;
            m_incomingMessageQueue.reserve(nextSize);
        }
    }

    // Process all queued messages
    int processedCount = 0;
    for (const auto& msg : m_processingMessageQueue) {
        if (msg.targetEntity == nullptr) {
            // This is a broadcast message
            deliverBroadcastMessage(msg.message);
        } else {
            // This is a targeted message
            deliverMessageToEntity(msg.targetEntity, msg.message);
        }
        processedCount++;
    }

    // Clear the processing queue for next time
    m_processingMessageQueue.clear();

    auto endTime = getCurrentTimeNanos();
    double processingTimeMs = (endTime - startTime) / 1000000.0;
    m_messageQueueStats.addSample(processingTimeMs);

    if (processedCount > 0) {
        AI_LOG("Message Queue: Processed " << processedCount << " messages in "
              << processingTimeMs << "ms (avg: " << m_messageQueueStats.averageUpdateTimeMs << "ms)");
    }

    // Reset processing flag
    m_processingMessages = false;
}

void AIManager::deliverMessageToEntity(Entity* entity, const std::string& message) {
    if (!entity) return;

    auto it = m_entityBehaviors.find(entity);
    if (it != m_entityBehaviors.end()) {
        // Store the behavior name before lookup
        const std::string& behaviorName = it->second;

        // Look up behavior just once
        AIBehavior* behavior = getBehavior(behaviorName);
        if (behavior) {
            AI_LOG_DETAIL("[AI Message] Delivering message to entity at ("
                    << entity->getPosition().getX() << "," << entity->getPosition().getY()
                    << ") with behavior '" << behaviorName << "': " << message);
            behavior->onMessage(entity, message);
        }
    }
}

size_t AIManager::deliverBroadcastMessage(const std::string& message) {
    AI_LOG("[AI Broadcast] Broadcasting message to all entities: " << message);

    // Use the entity-behavior cache for faster iteration
    // Track how many entities receive the message
    size_t messageReceivedCount = 0;
    
    // Since the cache contains direct pointers to behaviors, we can avoid repeated lookups
    for (const auto& cache : m_entityBehaviorCache) {
        if (!cache.entity || !cache.behavior) continue;

        #ifdef AI_LOG_DETAIL
        AI_LOG_DETAIL("[AI Broadcast] Entity at (" << cache.entity->getPosition().getX()
                << "," << cache.entity->getPosition().getY() << ") with behavior '"
                << cache.behaviorName << "' receiving broadcast");
        #endif

        cache.behavior->onMessage(cache.entity, message);
        messageReceivedCount++;
    }

    // Log the final count of entities that received the message
    AI_LOG("[AI Broadcast] Message delivered to " << messageReceivedCount << " entities");
    
    return messageReceivedCount;
}

// Implementation moved to the end of the file

void AIManager::registerBehavior(const std::string& behaviorName, std::shared_ptr<AIBehavior> behavior) {
    if (!behavior) {
        AI_LOG("Attempted to register null behavior: " << behaviorName);
        return;
    }

    // Check if behavior already exists
    if (m_behaviors.find(behaviorName) != m_behaviors.end()) {
        AI_LOG("Behavior already registered: " << behaviorName << ". Replacing.");
    }

    // Store the behavior
    m_behaviors[behaviorName] = behavior;

    // Initialize performance stats for this behavior
    m_behaviorPerformanceStats[behaviorName].reset();

    // Invalidate caches since behavior collection changed
    invalidateOptimizationCaches();

    AI_LOG("Behavior registered: " << behaviorName);
}

bool AIManager::hasBehavior(const std::string& behaviorName) const {
    // Using faster count method instead of find + comparison
    return m_behaviors.count(behaviorName) > 0;
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
        AI_LOG("Attempted to assign behavior to null entity");
        return;
    }

    // Look up behavior only once
    auto behaviorIt = m_behaviors.find(behaviorName);
    if (behaviorIt == m_behaviors.end()) {
        AI_LOG("Behavior not found: " << behaviorName);
        return;
    }

    AIBehavior* behavior = behaviorIt->second.get();
    if (!behavior) {
        AI_LOG("Behavior pointer is null: " << behaviorName);
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
    AI_LOG_DETAIL("[AI Init] Initializing behavior '" << behaviorName
              << "' for entity at position (" << entity->getPosition().getX()
              << "," << entity->getPosition().getY() << ")");
    behavior->init(entity);

    AI_LOG("Behavior '" << behaviorName << "' assigned to entity");
}

void AIManager::unassignBehaviorFromEntity(Entity* entity) {
    if (!entity) {
        AI_LOG("Attempted to unassign behavior from null entity");
        return;
    }

    auto it = m_entityBehaviors.find(entity);
    if (it != m_entityBehaviors.end()) {
        // Store behavior name before map modifications
        const std::string& behaviorName = it->second;

        // Clean up the behavior
        AIBehavior* behavior = getBehavior(behaviorName);
        if (behavior) {
            behavior->clean(entity);
        }

        // Remove from map
        m_entityBehaviors.erase(it);

        // Invalidate caches since entity-behavior mapping changed
        invalidateOptimizationCaches();

        AI_LOG("Behavior '" << behaviorName << "' unassigned from entity");
    }
}

bool AIManager::entityHasBehavior(Entity* entity) const {
    if (!entity) return false;
    return m_entityBehaviors.find(entity) != m_entityBehaviors.end();
}

void AIManager::sendMessageToEntity(Entity* entity, const std::string& message, bool immediate) {
    if (!entity) return;

    if (immediate) {
        deliverMessageToEntity(entity, message);
    } else {
        // Create the message with move semantics for better performance
        QueuedMessage queuedMsg(entity, message, getCurrentTimeNanos());

        // Add to incoming queue with minimal lock time
        {
            std::lock_guard<std::mutex> lock(m_messageQueueMutex);
            m_incomingMessageQueue.push_back(std::move(queuedMsg));
        }

        AI_LOG_DETAIL("[AI Message] Queued message for entity at ("
                   << entity->getPosition().getX() << "," << entity->getPosition().getY()
                   << "): " << message);
    }
}

void AIManager::broadcastMessage(const std::string& message, bool immediate) {
    if (immediate) {
        deliverBroadcastMessage(message);
    } else {
        // Create the broadcast message with move semantics
        QueuedMessage queuedMsg(nullptr, message, getCurrentTimeNanos());

        // Add to incoming queue with minimal lock time
        {
            std::lock_guard<std::mutex> lock(m_messageQueueMutex);
            m_incomingMessageQueue.push_back(std::move(queuedMsg));
        }

        AI_LOG("[AI Broadcast] Queued broadcast message: " << message);
    }
}
