/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/AIManager.hpp"
#include "core/ThreadSystem.hpp"
#include <SDL3/SDL.h>

// AIManager implementation

AIManager::AIManager() 
    : m_initialized(false)
    , m_useThreading(true)
    , m_maxThreads(0) {
}

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
        bool threadSystemExists = Forge::ThreadSystem::Exists();
        m_useThreading.store(threadSystemExists, std::memory_order_release);

        // Set initialized flag
        m_initialized.store(true, std::memory_order_release);

        // Log threading status
        if (threadSystemExists) {
            std::cout << "Forge Game Engine - AIManager: ThreadSystem detected with "
                      << Forge::ThreadSystem::Instance().getThreadCount() << " threads" << std::endl;
        } else {
            std::cout << "Forge Game Engine - AIManager: ThreadSystem not available, using single-threaded mode" << std::endl;
        }

        std::cout << "Forge Game Engine - AIManager initialized" << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - AIManager initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void AIManager::clean() {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }

    std::cout << "Forge Game Engine - Cleaning up AIManager resources" << std::endl;

    // Clear all behaviors and entities - need to acquire all locks to be safe
    {
        std::unique_lock<std::shared_mutex> entityLock(m_entityMutex);
        std::unique_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        std::lock_guard<std::mutex> batchesLock(m_batchesMutex);
        std::lock_guard<std::mutex> perfLock(m_perfStatsMutex);
        
        m_behaviors.clear();
        m_entityBehaviors.clear();
        m_entityBehaviorCache.clear();
        m_behaviorBatches.clear();
        m_behaviorPerformanceStats.clear();
    }

    m_initialized.store(false, std::memory_order_release);
    std::cout << "Forge Game Engine - AIManager resources cleaned" << std::endl;
}

void AIManager::configureThreading(bool useThreading, unsigned int maxThreads) {
    m_useThreading.store(useThreading, std::memory_order_release);

    // If maxThreads is 0 (auto), set it to optimal thread count based on hardware
    if (maxThreads == 0) {
        // Get available hardware threads
        unsigned int hwThreads = std::thread::hardware_concurrency();

        // If we couldn't determine the number, default to 4
        if (hwThreads == 0) hwThreads = 4;

        // Reserve 1 thread for main game thread on systems with ≤ 4 cores
        // Reserve 2 threads for main game thread and other systems on systems with > 4 cores
        maxThreads = (hwThreads <= 4) ? hwThreads - 1 : hwThreads - 2;

        // Ensure we have at least 1 thread
        maxThreads = std::max(1u, maxThreads);
    }

    m_maxThreads = maxThreads;

    if (useThreading) {
        std::cout << "Forge Game Engine - AIManager: Threading enabled" << std::endl;
        std::cout << "Forge Game Engine - AIManager: Using " << m_maxThreads
                  << " threads (hardware concurrency: " << std::thread::hardware_concurrency() << ")" << std::endl;
    } else {
        std::cout << "Forge Game Engine - AIManager: Threading disabled" << std::endl;
    }
}

void AIManager::registerBehavior(const std::string& behaviorName, std::shared_ptr<AIBehavior> behavior) {
    if (!behavior) {
        std::cerr << "Forge Game Engine - Error: Attempt to register null behavior: " << behaviorName << std::endl;
        return;
    }
    
    std::unique_lock<std::shared_mutex> lock(m_behaviorsMutex);
    m_behaviors[behaviorName] = behavior;
    invalidateOptimizationCaches();
}

bool AIManager::hasBehavior(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
    return m_behaviors.find(name) != m_behaviors.end();
}

AIBehavior* AIManager::getBehavior(const std::string& behaviorName) const {
    std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
    auto it = m_behaviors.find(behaviorName);
    if (it != m_behaviors.end()) {
        return it->second.get();
    }
    return nullptr;
}

void AIManager::assignBehaviorToEntity(EntityPtr entity, const std::string& behaviorName) {
    if (!entity) {
        std::cerr << "Forge Game Engine - Error: Attempted to assign behavior to null entity" << std::endl;
        return;
    }

    // First check if behavior exists
    AIBehavior* behavior = nullptr;
    {
        std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
        auto it = m_behaviors.find(behaviorName);
        if (it == m_behaviors.end()) {
            std::cerr << "Forge Game Engine - Behavior '" << behaviorName << "' not registered" << std::endl;
            return;
        }
        behavior = it->second.get();
    }

    // Create weak pointer from shared pointer
    EntityWeakPtr entityWeak = entity;
    
    // Assign behavior to entity - use write lock
    {
        std::unique_lock<std::shared_mutex> lock(m_entityMutex);
        m_entityBehaviors[entityWeak] = behaviorName;
    }

    // Invalidate caches - atomic operations
    invalidateOptimizationCaches();
    
    // Initialize the entity with this behavior
    try {
        behavior->init(entity);
    } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Error initializing " << behaviorName 
                  << " for entity: " << e.what() << std::endl;
    }
}

void AIManager::unassignBehaviorFromEntity(EntityPtr entity) {
    if (!entity) {
        return;
    }
    
    try {
        // Create weak pointer from shared pointer
        EntityWeakPtr entityWeak = entity;
        
        // Get the behavior for this entity before removing it
        AIBehavior* behavior = nullptr;
        std::string behaviorName;
        {
            std::shared_lock<std::shared_mutex> entityLock(m_entityMutex);
            auto it = m_entityBehaviors.find(entityWeak);
            if (it != m_entityBehaviors.end()) {
                behaviorName = it->second;
            }
        }
        
        // Clean up the entity's frame counter in the behavior
        if (!behaviorName.empty()) {
            behavior = getBehavior(behaviorName);
            if (behavior) {
                try {
                    behavior->cleanupEntity(entity);
                } catch (...) {
                    // Silently catch cleanup errors but continue with unassignment
                }
            }
        }
        
        // Remove entity from behavior map - use write lock
        {
            std::unique_lock<std::shared_mutex> lock(m_entityMutex);
            m_entityBehaviors.erase(entityWeak);
        }
        
        // Invalidate caches - atomic operations
        invalidateOptimizationCaches();
        
        AI_LOG("Unassigned behavior from entity at " << entity.get());
    } catch (...) {
        // Catch any exceptions to prevent crashes
        std::cerr << "Forge Game Engine - AIManager: Error unassigning behavior from entity" << std::endl;
    }
}

bool AIManager::entityHasBehavior(EntityPtr entity) const {
    if (!entity) {
        return false;
    }

    EntityWeakPtr entityWeak = entity;

    // Check for entity in map - read lock
    std::shared_lock<std::shared_mutex> lock(m_entityMutex);
    return m_entityBehaviors.find(entityWeak) != m_entityBehaviors.end();
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

    // First ensure caches are valid
    ensureOptimizationCachesValid();

    // Process all behaviors in optimized batches
    batchUpdateAllBehaviors();

    // Process any pending messages
    processMessageQueue();
}

void AIManager::invalidateOptimizationCaches() {
    m_cacheValid.store(false, std::memory_order_release);
    m_batchesValid.store(false, std::memory_order_release);
}

void AIManager::ensureOptimizationCachesValid() {
    // First check if cache needs rebuilding
    if (!m_cacheValid.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        // Double-check after acquiring lock
        if (!m_cacheValid.load(std::memory_order_acquire)) {
            rebuildEntityBehaviorCache();
            m_cacheValid.store(true, std::memory_order_release);
        }
    }

    // Then check if batches need rebuilding
    if (!m_batchesValid.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(m_batchesMutex);
        // Double-check after acquiring lock
        if (!m_batchesValid.load(std::memory_order_acquire)) {
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
    for (const auto& [entityWeak, behaviorName] : m_entityBehaviors) {
        // Skip expired entity references
        if (entityWeak.expired()) continue;
        
        auto behaviorIt = m_behaviors.find(behaviorName);
        if (behaviorIt != m_behaviors.end()) {
            EntityBehaviorCache cacheEntry;
            cacheEntry.entityWeak = entityWeak;
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
    for (const auto& [entityWeak, behaviorName] : m_entityBehaviors) {
        // Skip expired entity references
        if (entityWeak.expired()) continue;
        
        // Get the shared pointer from the weak pointer
        EntityPtr entity = entityWeak.lock();
        if (!entity) continue;
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

        // Prepare for multi-level parallelism - parallelize across behaviors and within each behavior

        // Reserve capacity in ThreadSystem for maximum efficiency
        const size_t totalBatches = batchesCopy.size();
        if (totalBatches > 16) {
            // If we have many behavior types, ensure the queue has enough capacity
            Forge::ThreadSystem::Instance().reserveQueueCapacity(totalBatches * 2);

            // Enable debug logging if we have lots of behaviors
            if (totalBatches > 32) {
                Forge::ThreadSystem::Instance().setDebugLogging(true);
            }
        }

        // Determine if we should sub-divide larger batches for better parallelism
        const unsigned int availableThreads = m_maxThreads;
        const unsigned int behaviorsCount = static_cast<unsigned int>(batchesCopy.size());

        // If we have more threads than behaviors, we can parallelize within batches too
        const bool useIntraBatchParallelism = (availableThreads > behaviorsCount);

        // Create a vector to store futures for all batches
        std::vector<std::future<void>> batchFutures;
        batchFutures.reserve(totalBatches);

        // Process each behavior type in parallel
        for (const auto& [behaviorName, batch] : batchesCopy) {
            if (batch.empty()) continue;

            // Find the behavior
            AIBehavior* behavior = getBehavior(behaviorName);
            if (!behavior) continue;

            if (useIntraBatchParallelism && batch.size() >= 100) {
                // For large batches, use processEntitiesWithBehavior which applies internal parallelism
                auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult(
                    [this, behavior, batch, behaviorName]() {
                        auto startTime = getCurrentTimeNanos();
                        
                        // Use multi-threaded processing within this batch 
                        this->processEntitiesWithBehavior(behavior, batch, true);
                        
                        auto endTime = getCurrentTimeNanos();
                        double timeMs = (endTime - startTime) / 1000000.0;
                        recordBehaviorPerformance(behaviorName, timeMs);
                    },
                    Forge::TaskPriority::High,
                    "AI Batch Update: " + behaviorName + " (" + std::to_string(batch.size()) + " entities, parallel)");

                batchFutures.push_back(std::move(future));
            } else {
                // For smaller batches, use standard single-threaded processing
                auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult(
                    [this, behavior, batch, behaviorName]() {
                        auto startTime = getCurrentTimeNanos();

                        // Process all entities with this behavior
                        for (const EntityPtr& entity : batch) {
                            if (entity) {
                                try {
                                    // Always increment the frame counter before checking if we should update
                                    behavior->incrementFrameCounter(entity);

                                    // Only update if the behavior's shouldUpdate returns true
                                    // This will check the frame counter internally
                                    if (behavior->shouldUpdate(entity)) {
                                        behavior->update(entity);
                                        // Reset frame counter after update
                                        behavior->resetFrameCounter(entity);
                                    }

                                    // Check if entity has gone off-screen and needs cleanup
                                    if (entity->getPosition().getX() < -2000 ||
                                        entity->getPosition().getX() > 3000 ||
                                        entity->getPosition().getY() < -2000 ||
                                        entity->getPosition().getY() > 3000) {
                                        // Reset frame counter to encourage update
                                        behavior->setFrameCounter(entity, 999);
                                    }
                                } catch (const std::exception& e) {
                                    // Log exception details but continue processing other entities
                                    std::cerr << "Exception updating entity with " << behaviorName
                                              << ": " << e.what() << std::endl;
                                    continue;
                                } catch (...) {
                                    // Catch any exceptions to prevent thread crashes
                                    std::cerr << "Unknown exception updating entity with "
                                              << behaviorName << std::endl;
                                    continue;
                                }
                            }
                        }

                        // Record performance stats
                        auto endTime = getCurrentTimeNanos();
                        double timeMs = (endTime - startTime) / 1000000.0;
                        recordBehaviorPerformance(behaviorName, timeMs);
                    },
                    Forge::TaskPriority::Normal,
                    "AI Batch Update: " + behaviorName + " (" + std::to_string(batch.size()) + " entities)");

                batchFutures.push_back(std::move(future));
            }
        }

        // Wait for all batch updates to complete
        for (auto& future : batchFutures) {
            try {
                future.get();
            } catch (const std::exception& e) {
                // Log the exception but continue processing other batches
                std::cerr << "Exception in AI batch update: " << e.what() << std::endl;
            } catch (...) {
                // Catch any other exceptions
                std::cerr << "Unknown exception in AI batch update" << std::endl;
            }
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
    if (batch.empty()) {
        return;
    }

    // Get the behavior
    AIBehavior* behavior = nullptr;
    {
        std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
        auto it = m_behaviors.find(std::string(behaviorName));
        if (it == m_behaviors.end() || !it->second) {
            return;
        }
        behavior = it->second.get();
    }

    // Determine if we should use threading for this batch
    const bool useThreadingForBatch = m_useThreading.load(std::memory_order_acquire) &&
                                      Forge::ThreadSystem::Exists() &&
                                      batch.size() > 200; // Threshold for threading a single batch

    auto startTime = getCurrentTimeNanos();

    if (useThreadingForBatch) {
        // Process the batch in parallel
        processEntitiesWithBehavior(behavior, batch, true);
    } else {
        // Process the batch sequentially
        for (const EntityPtr& entity : batch) {
            if (entity) {
                try {
                    // Always increment the frame counter
                    behavior->incrementFrameCounter(entity);

                    // Only update if behavior's shouldUpdate returns true
                    if (behavior->shouldUpdate(entity)) {
                        behavior->update(entity);
                        behavior->resetFrameCounter(entity);
                    }

                    // Check if entity has gone off-screen and needs cleanup
                    if (entity->getPosition().getX() < -2000 ||
                        entity->getPosition().getX() > 3000 ||
                        entity->getPosition().getY() < -2000 ||
                        entity->getPosition().getY() > 3000) {
                        // Reset frame counter to encourage update
                        behavior->setFrameCounter(entity, 999);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Exception updating entity with " << behaviorName
                              << ": " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Unknown exception updating entity with " << behaviorName << std::endl;
                }
            }
        }
    }

    // Record performance data
    auto endTime = getCurrentTimeNanos();
    double timeMs = (endTime - startTime) / 1000000.0;
    recordBehaviorPerformance(behaviorName, timeMs);
}

void AIManager::batchProcessEntities(const std::string& behaviorName, const std::vector<EntityPtr>& entities) {
    if (entities.empty()) return;
    
    // Get the behavior
    AIBehavior* behavior = getBehavior(behaviorName);
    if (!behavior) return;
    
    // Process entities with or without threading
    bool useThreading = m_useThreading.load(std::memory_order_acquire) && 
                         Forge::ThreadSystem::Exists();
    
    processEntitiesWithBehavior(behavior, entities, useThreading);
}

void AIManager::processEntitiesWithBehavior(AIBehavior* behavior, const std::vector<EntityPtr>& entities, bool useThreading) {
    if (!behavior || entities.empty()) return;
    
    auto startTime = getCurrentTimeNanos();
    
    if (useThreading) {
        // Split entities into chunks for parallel processing
        const size_t numEntities = entities.size();
        
        // Determine optimal thread count based on workload characteristics
        const unsigned int configuredMaxThreads = m_maxThreads;
        
        // Adaptive thread allocation based on workload size and characteristics
        // Fine-tune thread count based on entity count for optimal performance
        unsigned int effectiveThreadCount;
        
        // Determine optimal chunk size based on entity count
        // Small entity counts: use more threads with smaller chunks for responsiveness
        // Large entity counts: use fewer threads with larger chunks for efficiency
        size_t targetChunkSize;
        
        if (numEntities < 50) {
            // Very small batches - use minimal threading to avoid overhead
            effectiveThreadCount = 2;
            targetChunkSize = 10;
        } else if (numEntities < 200) {
            // Small batches - use moderate threading
            effectiveThreadCount = std::min(4u, configuredMaxThreads);
            targetChunkSize = 25;
        } else if (numEntities < 1000) {
            // Medium batches - scale up threading
            effectiveThreadCount = std::min(configuredMaxThreads, 8u);
            targetChunkSize = 50;
        } else if (numEntities < 5000) {
            // Large batches - use most available threads
            effectiveThreadCount = configuredMaxThreads;
            targetChunkSize = 100;
        } else {
            // Very large batches - use all threads but larger chunks
            effectiveThreadCount = configuredMaxThreads;
            targetChunkSize = 250;
        }

        // Calculate optimal number of threads based on entity count and target chunk size
        const size_t calculatedThreads = (numEntities + targetChunkSize - 1) / targetChunkSize;
        
        // Use the lower of calculated or effective thread count to avoid creating too many tiny tasks
        const size_t numThreads = std::min(calculatedThreads, static_cast<size_t>(effectiveThreadCount));
        
        // Ensure we have at least one thread and no more than the entity count
        const size_t finalNumThreads = std::max(static_cast<size_t>(1), std::min(numThreads, numEntities));
        
        // Recalculate chunk size to ensure even distribution
        const size_t chunkSize = (numEntities + finalNumThreads - 1) / finalNumThreads;

        // Reserve thread system capacity if we're creating a lot of tasks
        if (finalNumThreads > 16) {
            Forge::ThreadSystem::Instance().reserveQueueCapacity(finalNumThreads);
            // Enable debug logging for very large entity batches
            if (finalNumThreads > 32) {
                Forge::ThreadSystem::Instance().setDebugLogging(true);
            }
        }
        
        std::vector<std::future<void>> futures;
        futures.reserve(finalNumThreads);
        
        // Submit tasks in chunks
        for (size_t i = 0; i < finalNumThreads; ++i) {
            size_t startIdx = i * chunkSize;
            size_t endIdx = std::min(startIdx + chunkSize, numEntities);
            
            if (startIdx >= numEntities) break;
            
            std::string taskDescription = "AI Entity Processing: Chunk " + 
                                         std::to_string(startIdx) + "-" + 
                                         std::to_string(endIdx) + 
                                         " (" + std::to_string(endIdx - startIdx) + " entities)";
            
            futures.push_back(Forge::ThreadSystem::Instance().enqueueTaskWithResult(
                [behavior, &entities, startIdx, endIdx]() {
                    try {
                        for (size_t j = startIdx; j < endIdx; ++j) {
                            const EntityPtr& entity = entities[j];
                            if (entity) {
                                // Always increment the frame counter before checking if we should update
                                behavior->incrementFrameCounter(entity);
                                
                                // Only update if the behavior's shouldUpdate returns true
                                if (behavior->shouldUpdate(entity)) {
                                    behavior->update(entity);
                                    behavior->resetFrameCounter(entity);
                                }

                                // Check if entity has gone off-screen and needs cleanup
                                if (entity->getPosition().getX() < -2000 || 
                                    entity->getPosition().getX() > 3000 ||
                                    entity->getPosition().getY() < -2000 || 
                                    entity->getPosition().getY() > 3000) {
                                    // Reset frame counter to encourage update
                                    behavior->setFrameCounter(entity, 999);
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        // Log exception details
                        std::cerr << "Exception in AI thread: " << e.what() << std::endl;
                    } catch (...) {
                        // Catch any exceptions to prevent thread crashes
                        std::cerr << "Unknown exception in AI thread" << std::endl;
                    }
            },
            Forge::TaskPriority::Normal,
            taskDescription));
    }

        // Wait for all tasks to complete and handle exceptions
        for (auto& future : futures) {
            try {
                future.get();
            } catch (const std::exception& e) {
                // Log the exception but continue processing
                std::cerr << "Exception in AI thread: " << e.what() << std::endl;
            } catch (...) {
                // Catch any other exceptions during execution
                std::cerr << "Unknown exception in AI thread" << std::endl;
            }
        }
    } else {
        // Sequential processing
        for (const EntityPtr& entity : entities) {
            if (entity) {
                try {
                    // Always increment the frame counter before checking if we should update
                    behavior->incrementFrameCounter(entity);
                    
                    // Only update if the behavior's shouldUpdate returns true
                    if (behavior->shouldUpdate(entity)) {
                        behavior->update(entity);
                        behavior->resetFrameCounter(entity);
                    }
                    
                    // Check if entity has gone off-screen and needs cleanup
                    if (entity->getPosition().getX() < -2000 || 
                        entity->getPosition().getX() > 3000 ||
                        entity->getPosition().getY() < -2000 || 
                        entity->getPosition().getY() > 3000) {
                        // Reset frame counter to encourage update
                        behavior->setFrameCounter(entity, 999);
                    }
                } catch (const std::exception& e) {
                    // Log exception details
                    std::cerr << "Exception in AI update: " << e.what() << std::endl;
                    continue;
                } catch (...) {
                    // Catch any exceptions during update
                    std::cerr << "Unknown exception in AI update" << std::endl;
                    continue;
                }
            }
        }
    }
    
    // Record performance metrics
    auto endTime = getCurrentTimeNanos();
    double timeMs = (endTime - startTime) / 1000000.0;
    recordBehaviorPerformance(behavior->getName(), timeMs);
}

void AIManager::sendMessageToEntity(EntityPtr entity, const std::string& message, bool immediate) {
    if (!entity) return;
    
    // Create weak pointer from shared pointer
    EntityWeakPtr entityWeak = entity;
    
    if (immediate) {
        deliverMessageToEntity(entity, message);
    } else {
        m_messageQueue.enqueueMessage(entityWeak, message);
    }
}

void AIManager::broadcastMessage(const std::string& message, bool immediate) {
    if (immediate) {
        deliverBroadcastMessage(message);
    } else {
        EntityWeakPtr nullEntity; // Empty weak_ptr for broadcast
        m_messageQueue.enqueueMessage(nullEntity, message);
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
        if (!msg.targetEntity.expired()) {
            // If the target entity still exists, deliver the message
            EntityPtr entity = msg.targetEntity.lock();
            if (entity) {
                deliverMessageToEntity(entity, msg.message);
            }
        } else {
            // Empty weak_ptr indicates broadcast
            deliverBroadcastMessage(msg.message);
        }
    }
    
    // Record performance metrics
    auto endTime = getCurrentTimeNanos();
    
    {
        double timeMs = (endTime - startTime) / 1000000.0;
        std::lock_guard<std::mutex> lock(m_perfStatsMutex);
        m_messageQueueStats.addSample(timeMs);
    }
    
    // Reset processing flag
    m_processingMessages.store(false, std::memory_order_release);
}

void AIManager::deliverMessageToEntity(EntityPtr entity, const std::string& message) {
    if (!entity) return;
    
    // Create weak pointer from shared pointer
    EntityWeakPtr entityWeak = entity;
    
    // Get entity's behavior
    AIBehavior* behavior = nullptr;
    
    // First try to find in cache for efficiency
    if (m_cacheValid.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        auto it = std::find_if(m_entityBehaviorCache.begin(), m_entityBehaviorCache.end(),
            [&entityWeak](const EntityBehaviorCache& cache) { 
                return !cache.entityWeak.expired() && !entityWeak.expired() && 
                       cache.entityWeak.lock() == entityWeak.lock(); 
            });
        
        if (it != m_entityBehaviorCache.end()) {
            behavior = it->behavior;
        }
    }
    
    // If not found in cache, check the map
    if (!behavior) {
        std::string behaviorName;
        
        {
            std::shared_lock<std::shared_mutex> entityLock(m_entityMutex);
            auto it = m_entityBehaviors.find(entityWeak);
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
        AI_LOG("Delivered message '" << message << "' to entity at " << entity.get());
    }
}

size_t AIManager::deliverBroadcastMessage(const std::string& message) {
    size_t deliveredCount = 0;
    
    // Use cache if valid for efficiency
    if (m_cacheValid.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        
        for (const auto& cache : m_entityBehaviorCache) {
            if (cache.behavior && !cache.entityWeak.expired()) {
                EntityPtr entity = cache.entityWeak.lock();
                if (entity) {
                    cache.behavior->onMessage(entity, message);
                    deliveredCount++;
                }
            }
        }
    } else {
        // Otherwise use entity-behavior map
        std::shared_lock<std::shared_mutex> entityLock(m_entityMutex);
        std::shared_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);
        
        for (const auto& [entityWeak, behaviorName] : m_entityBehaviors) {
            if (!entityWeak.expired()) {
                EntityPtr entity = entityWeak.lock();
                auto it = m_behaviors.find(behaviorName);
                if (it != m_behaviors.end() && it->second && entity) {
                    it->second->onMessage(entity, message);
                    deliveredCount++;
                }
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
    // First, send release_entities message to all behaviors
    try {
        broadcastMessage("release_entities", true);
        processMessageQueue();
    } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Exception during behavior release: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Forge Game Engine - Unknown exception during behavior release" << std::endl;
    }
    
    // Stop message processing
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
