/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/AIManager.hpp"
#include "core/ThreadSystem.hpp"
#include <iostream>
#include <algorithm>

// AIManager constructor
AIManager::AIManager() 
    : m_initialized(false),
      m_useThreading(true),
      m_maxThreads(0) {
    AI_LOG("AIManager created");
}

// AIManager destructor
AIManager::~AIManager() {
    if (m_initialized.load(std::memory_order_acquire)) {
        clean();
    }
}

bool AIManager::init() {
    if (m_initialized.load(std::memory_order_acquire)) {
        std::cout << "Forge Game Engine - AIManager already initialized" << std::endl;
        return true;
    }

    try {
        // Initialize behavior batches
        m_batchesValid.store(false, std::memory_order_release);
        m_cacheValid.store(false, std::memory_order_release);
        
        // Set threading mode based on ThreadSystem availability
        m_useThreading.store(Forge::ThreadSystem::Exists(), std::memory_order_release);
        
        // Set initialized flag
        m_initialized.store(true, std::memory_order_release);
        
        std::cout << "Forge Game Engine - AIManager initialized" << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - AIManager initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void AIManager::clean() {
    // Only clean once
    if (!m_initialized.exchange(false)) {
        return;
    }
    
    // Stop all processing
    m_processingMessages.store(false, std::memory_order_release);
    
    // Clear all message queues
    m_messageQueue.clear();
    
    // Acquire all locks to ensure no operations are in progress
    std::unique_lock<std::shared_mutex> entityLock(m_entityMutex);
    std::unique_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);
    std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
    std::lock_guard<std::mutex> batchesLock(m_batchesMutex);
    std::lock_guard<std::mutex> perfLock(m_perfStatsMutex);
    
    // Clear all data structures
    m_behaviors.clear();
    m_entityBehaviors.clear();
    m_entityBehaviorCache.clear();
    m_behaviorBatches.clear();
    m_behaviorPerformanceStats.clear();
    
    // Reset flags
    m_cacheValid.store(false, std::memory_order_release);
    m_batchesValid.store(false, std::memory_order_release);
    
    std::cout << "Forge Game Engine - AIManager resources cleaned!" << std::endl;
}

void AIManager::configureThreading(bool useThreading, unsigned int maxThreads) {
    m_useThreading.store(useThreading, std::memory_order_release);
    m_maxThreads = maxThreads;
    
    if (useThreading) {
        std::cout << "Forge Game Engine - AIManager: Threading enabled" << std::endl;
        if (maxThreads > 0) {
            std::cout << "Forge Game Engine - AIManager: Using " << maxThreads << " threads max" << std::endl;
        } else {
            std::cout << "Forge Game Engine - AIManager: Using auto thread count" << std::endl;
        }
    } else {
        std::cout << "Forge Game Engine - AIManager: Threading disabled" << std::endl;
    }
}

void AIManager::registerBehavior(const std::string& behaviorName, std::shared_ptr<AIBehavior> behavior) {
    if (!behavior) {
        std::cerr << "Forge Game Engine - AIManager: Cannot register null behavior" << std::endl;
        return;
    }
    
    // Lock behaviors map for writing
    std::unique_lock<std::shared_mutex> lock(m_behaviorsMutex);
    
    // Add the behavior
    m_behaviors[behaviorName] = std::move(behavior);
    
    std::cout << "Forge Game Engine - AIManager: Registered behavior '" << behaviorName << "'" << std::endl;
}

bool AIManager::hasBehavior(const std::string& behaviorName) const {
    // Read-only access to behaviors
    std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
    return m_behaviors.find(behaviorName) != m_behaviors.end();
}

AIBehavior* AIManager::getBehavior(const std::string& behaviorName) const {
    // Read-only access to behaviors
    std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
    
    auto it = m_behaviors.find(behaviorName);
    if (it != m_behaviors.end()) {
        return it->second.get();
    }
    
    return nullptr;
}

void AIManager::assignBehaviorToEntity(Entity* entity, const std::string& behaviorName) {
    if (!entity) {
        std::cerr << "Forge Game Engine - AIManager: Cannot assign behavior to null entity" << std::endl;
        return;
    }
    
    // Check if behavior exists - use read lock
    {
        std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
        if (m_behaviors.find(behaviorName) == m_behaviors.end()) {
            std::cerr << "Forge Game Engine - AIManager: Behavior '" << behaviorName 
                      << "' not registered" << std::endl;
            return;
        }
    }
    
    // Assign behavior to entity - use write lock
    {
        std::unique_lock<std::shared_mutex> lock(m_entityMutex);
        m_entityBehaviors[entity] = behaviorName;
    }
    
    // Invalidate caches - atomic operations
    invalidateOptimizationCaches();
    
    AI_LOG("Assigned behavior '" << behaviorName << "' to entity at " << entity);
}

void AIManager::unassignBehaviorFromEntity(Entity* entity) {
    if (!entity) {
        return;
    }
    
    // Remove entity from behavior map - use write lock
    {
        std::unique_lock<std::shared_mutex> lock(m_entityMutex);
        m_entityBehaviors.erase(entity);
    }
    
    // Invalidate caches - atomic operations
    invalidateOptimizationCaches();
    
    AI_LOG("Unassigned behavior from entity at " << entity);
}

bool AIManager::entityHasBehavior(Entity* entity) const {
    if (!entity) {
        return false;
    }
    
    // Read-only access to entity behaviors
    std::shared_lock<std::shared_mutex> lock(m_entityMutex);
    return m_entityBehaviors.find(entity) != m_entityBehaviors.end();
}

bool AIManager::hasEntityWithBehavior(const std::string& behaviorName) const {
    // First check if cache is valid - might avoid locking
    if (m_batchesValid.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(m_batchesMutex);
        auto it = m_behaviorBatches.find(behaviorName);
        if (it != m_behaviorBatches.end()) {
            return !it->second.empty();
        }
    }
    
    // Fallback to entity check
    std::shared_lock<std::shared_mutex> lock(m_entityMutex);
    for (const auto& [entity, behavior] : m_entityBehaviors) {
        if (behavior == behaviorName) {
            return true;
        }
    }
    
    return false;
}

void AIManager::update() {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }
    
    // First process any pending messages
    processMessageQueue();
    
    // Then update all behaviors in optimized batches
    batchUpdateAllBehaviors();
}

void AIManager::invalidateOptimizationCaches() {
    m_cacheValid.store(false, std::memory_order_release);
    m_batchesValid.store(false, std::memory_order_release);
}

void AIManager::ensureOptimizationCachesValid() {
    // Check entity cache validity
    if (!m_cacheValid.load(std::memory_order_acquire)) {
        // Lock and double-check pattern
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (!m_cacheValid.load(std::memory_order_relaxed)) {
            rebuildEntityBehaviorCache();
            m_cacheValid.store(true, std::memory_order_release);
        }
    }
    
    // Check batch validity
    if (!m_batchesValid.load(std::memory_order_acquire)) {
        // Lock and double-check pattern
        std::lock_guard<std::mutex> lock(m_batchesMutex);
        if (!m_batchesValid.load(std::memory_order_relaxed)) {
            rebuildBehaviorBatches();
            m_batchesValid.store(true, std::memory_order_release);
        }
    }
}

void AIManager::rebuildEntityBehaviorCache() {
    // Clear existing cache
    m_entityBehaviorCache.clear();
    
    // Read entity-behavior mappings - read lock
    std::shared_lock<std::shared_mutex> entityLock(m_entityMutex);
    std::shared_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);
    
    // Rebuild cache with latest mappings
    for (const auto& [entity, behaviorName] : m_entityBehaviors) {
        auto behaviorIt = m_behaviors.find(behaviorName);
        if (behaviorIt != m_behaviors.end()) {
            EntityBehaviorCache cacheEntry;
            cacheEntry.entity = entity;
            cacheEntry.behavior = behaviorIt->second.get();
            cacheEntry.behaviorName = behaviorName;
            cacheEntry.lastUpdateTime = getCurrentTimeNanos();
            
            m_entityBehaviorCache.push_back(std::move(cacheEntry));
        }
    }
    
    AI_LOG("Rebuilt entity behavior cache with " << m_entityBehaviorCache.size() << " entries");
}

void AIManager::rebuildBehaviorBatches() {
    // Clear existing batches
    m_behaviorBatches.clear();
    
    // Read entity-behavior mappings - read lock
    std::shared_lock<std::shared_mutex> lock(m_entityMutex);
    
    // Group entities by behavior
    for (const auto& [entity, behaviorName] : m_entityBehaviors) {
        m_behaviorBatches[behaviorName].push_back(entity);
    }
    
    AI_LOG("Rebuilt behavior batches with " << m_behaviorBatches.size() << " behavior types");
}

void AIManager::batchUpdateAllBehaviors() {
    // Ensure caches are valid
    ensureOptimizationCachesValid();
    
    // Only use threading if enabled and ThreadSystem is available
    bool useThreading = m_useThreading.load(std::memory_order_acquire) && 
                         Forge::ThreadSystem::Exists();
    
    if (useThreading) {
        // Create a copy of behavior batch map to avoid lock contention during processing
        boost::container::flat_map<std::string, BehaviorBatch> batchesCopy;
        {
            std::lock_guard<std::mutex> lock(m_batchesMutex);
            batchesCopy = m_behaviorBatches;
        }
        
        // Process each behavior type in parallel
        for (const auto& [behaviorName, batch] : batchesCopy) {
            if (batch.empty()) continue;
            
            // Find the behavior
            AIBehavior* behavior = getBehavior(behaviorName);
            if (!behavior) continue;
            
            // Queue this batch for parallel processing
            Forge::ThreadSystem::Instance().enqueueTask([this, behavior, batch, behaviorName]() {
                auto startTime = getCurrentTimeNanos();
                
                // Process all entities with this behavior
                for (Entity* entity : batch) {
                    if (entity) behavior->update(entity);
                }
                
                // Record performance stats
                auto endTime = getCurrentTimeNanos();
                double timeMs = (endTime - startTime) / 1000000.0;
                recordBehaviorPerformance(behaviorName, timeMs);
            });
        }
    } else {
        // Fallback to sequential processing if threading is disabled
        std::lock_guard<std::mutex> lock(m_batchesMutex);
        for (const auto& [behaviorName, batch] : m_behaviorBatches) {
            if (!batch.empty()) {
                updateBehaviorBatch(behaviorName, batch);
            }
        }
    }
}

void AIManager::updateBehaviorBatch(const std::string_view& behaviorName, const BehaviorBatch& batch) {
    if (batch.empty()) return;
    
    // Get the behavior
    AIBehavior* behavior = nullptr;
    {
        std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
        auto it = m_behaviors.find(std::string(behaviorName));
        if (it != m_behaviors.end()) {
            behavior = it->second.get();
        }
    }
    
    if (!behavior) return;
    
    // Update all entities with this behavior
    auto startTime = getCurrentTimeNanos();
    
    for (Entity* entity : batch) {
        if (entity) {
            behavior->update(entity);
        }
    }
    
    // Record performance metrics
    auto endTime = getCurrentTimeNanos();
    double timeMs = (endTime - startTime) / 1000000.0;
    recordBehaviorPerformance(behaviorName, timeMs);
}

void AIManager::batchProcessEntities(const std::string& behaviorName, const std::vector<Entity*>& entities) {
    if (entities.empty()) return;
    
    // Get the behavior
    AIBehavior* behavior = getBehavior(behaviorName);
    if (!behavior) return;
    
    // Process entities with or without threading
    bool useThreading = m_useThreading.load(std::memory_order_acquire) && 
                         Forge::ThreadSystem::Exists();
    
    processEntitiesWithBehavior(behavior, entities, useThreading);
}

void AIManager::processEntitiesWithBehavior(AIBehavior* behavior, const std::vector<Entity*>& entities, bool useThreading) {
    if (!behavior || entities.empty()) return;
    
    auto startTime = getCurrentTimeNanos();
    
    if (useThreading) {
        // Split entities into chunks for parallel processing
        const size_t numEntities = entities.size();
        const size_t numThreads = std::min(numEntities, static_cast<size_t>(4)); // Use up to 4 threads
        const size_t chunkSize = (numEntities + numThreads - 1) / numThreads;
        
        std::vector<std::future<void>> futures;
        futures.reserve(numThreads);
        
        for (size_t i = 0; i < numThreads; ++i) {
            size_t startIdx = i * chunkSize;
            size_t endIdx = std::min(startIdx + chunkSize, numEntities);
            
            if (startIdx >= numEntities) break;
            
            futures.push_back(Forge::ThreadSystem::Instance().enqueueTaskWithResult([behavior, &entities, startIdx, endIdx]() {
                for (size_t j = startIdx; j < endIdx; ++j) {
                    Entity* entity = entities[j];
                    if (entity) {
                        behavior->update(entity);
                    }
                }
            }));
        }
        
        // Wait for all tasks to complete
        for (auto& future : futures) {
            future.wait();
        }
    } else {
        // Sequential processing
        for (Entity* entity : entities) {
            if (entity) {
                behavior->update(entity);
            }
        }
    }
    
    // Record performance metrics
    auto endTime = getCurrentTimeNanos();
    double timeMs = (endTime - startTime) / 1000000.0;
    recordBehaviorPerformance(behavior->getName(), timeMs);
}

void AIManager::sendMessageToEntity(Entity* entity, const std::string& message, bool immediate) {
    if (!entity) return;
    
    if (immediate) {
        deliverMessageToEntity(entity, message);
    } else {
        m_messageQueue.enqueueMessage(entity, message);
    }
}

void AIManager::broadcastMessage(const std::string& message, bool immediate) {
    if (immediate) {
        deliverBroadcastMessage(message);
    } else {
        m_messageQueue.enqueueMessage(nullptr, message);
    }
}

void AIManager::processMessageQueue() {
    // Check if we're already processing messages
    if (m_processingMessages.exchange(true)) return;
    
    // Get performance start time
    auto startTime = getCurrentTimeNanos();
    
    // Swap buffers to minimize lock time
    m_messageQueue.swapBuffers();
    
    // Process messages - now lock-free since we're using our own buffer
    const auto& messages = m_messageQueue.getProcessingQueue();
    for (const auto& msg : messages) {
        if (msg.targetEntity) {
            deliverMessageToEntity(msg.targetEntity, msg.message);
        } else {
            deliverBroadcastMessage(msg.message);
        }
    }
    
    // Record performance metrics
    auto endTime = getCurrentTimeNanos();
    double timeMs = (endTime - startTime) / 1000000.0;
    
    {
        std::lock_guard<std::mutex> lock(m_perfStatsMutex);
        m_messageQueueStats.addSample(timeMs);
    }
    
    // Reset processing flag
    m_processingMessages.store(false, std::memory_order_release);
}

void AIManager::deliverMessageToEntity(Entity* entity, const std::string& message) {
    if (!entity) return;
    
    // Get entity's behavior
    AIBehavior* behavior = nullptr;
    
    // First try to find in cache for efficiency
    if (m_cacheValid.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        auto it = std::find_if(m_entityBehaviorCache.begin(), m_entityBehaviorCache.end(),
            [entity](const EntityBehaviorCache& cache) { return cache.entity == entity; });
        
        if (it != m_entityBehaviorCache.end()) {
            behavior = it->behavior;
        }
    }
    
    // If not found in cache, check the map
    if (!behavior) {
        std::string behaviorName;
        
        {
            std::shared_lock<std::shared_mutex> entityLock(m_entityMutex);
            auto it = m_entityBehaviors.find(entity);
            if (it != m_entityBehaviors.end()) {
                behaviorName = it->second;
            }
        }
        
        if (!behaviorName.empty()) {
            std::shared_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);
            auto it = m_behaviors.find(behaviorName);
            if (it != m_behaviors.end()) {
                behavior = it->second.get();
            }
        }
    }
    
    // Deliver message if behavior found
    if (behavior) {
        behavior->onMessage(entity, message);
        AI_LOG("Delivered message '" << message << "' to entity at " << entity);
    }
}

size_t AIManager::deliverBroadcastMessage(const std::string& message) {
    size_t deliveredCount = 0;
    
    // Use cache if valid for efficiency
    if (m_cacheValid.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        
        for (const auto& cache : m_entityBehaviorCache) {
            if (cache.behavior && cache.entity) {
                cache.behavior->onMessage(cache.entity, message);
                deliveredCount++;
            }
        }
    } else {
        // Otherwise use entity-behavior map
        std::shared_lock<std::shared_mutex> entityLock(m_entityMutex);
        std::shared_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);
        
        for (const auto& [entity, behaviorName] : m_entityBehaviors) {
            auto it = m_behaviors.find(behaviorName);
            if (it != m_behaviors.end() && it->second && entity) {
                it->second->onMessage(entity, message);
                deliveredCount++;
            }
        }
    }
    
    AI_LOG("Broadcast message '" << message << "' to " << deliveredCount << " entities");
    return deliveredCount;
}

void AIManager::recordBehaviorPerformance(const std::string_view& behaviorName, double timeMs) {
    std::lock_guard<std::mutex> lock(m_perfStatsMutex);
    m_behaviorPerformanceStats[std::string(behaviorName)].addSample(timeMs);
}

void AIManager::resetBehaviors() {
    // First, stop message processing
    m_processingMessages.store(false, std::memory_order_release);
    m_messageQueue.clear();
    
    // Clear entity assignments but keep registered behaviors
    {
        std::unique_lock<std::shared_mutex> lock(m_entityMutex);
        m_entityBehaviors.clear();
    }
    
    // Clear caches
    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        m_entityBehaviorCache.clear();
    }
    
    {
        std::lock_guard<std::mutex> batchesLock(m_batchesMutex);
        m_behaviorBatches.clear();
    }
    
    // Reset stats
    {
        std::lock_guard<std::mutex> perfLock(m_perfStatsMutex);
        m_behaviorPerformanceStats.clear();
        m_messageQueueStats.reset();
    }
    
    // Invalidate caches
    m_cacheValid.store(false, std::memory_order_release);
    m_batchesValid.store(false, std::memory_order_release);
    
    std::cout << "Forge Game Engine - AIManager: Behaviors reset" << std::endl;
}

size_t AIManager::getBehaviorCount() const {
    std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
    return m_behaviors.size();
}

size_t AIManager::getManagedEntityCount() const {
    std::shared_lock<std::shared_mutex> lock(m_entityMutex);
    return m_entityBehaviors.size();
}