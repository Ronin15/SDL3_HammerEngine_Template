/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/AIManager.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include <algorithm>
#include <cstring>

bool AIManager::init() {
  if (m_initialized.load(std::memory_order_acquire)) {
    AI_LOG("AIManager already initialized");
    return true;
  }

  try {
    // Initialize behavior type mappings
    m_behaviorTypeMap["Wander"] = BehaviorType::Wander;
    m_behaviorTypeMap["Guard"] = BehaviorType::Guard;
    m_behaviorTypeMap["Patrol"] = BehaviorType::Patrol;
    m_behaviorTypeMap["Follow"] = BehaviorType::Follow;
    m_behaviorTypeMap["Chase"] = BehaviorType::Chase;
    m_behaviorTypeMap["Attack"] = BehaviorType::Attack;
    m_behaviorTypeMap["Flee"] = BehaviorType::Flee;
    m_behaviorTypeMap["Idle"] = BehaviorType::Idle;

    // Pre-allocate storage for better performance
    constexpr size_t INITIAL_CAPACITY = 1000;
    m_storage.reserve(INITIAL_CAPACITY);
    m_entityToIndex.reserve(INITIAL_CAPACITY);

    // Reserve capacity for assignment queues to prevent reallocations during
    // batch processing
    m_pendingAssignments.reserve(AIConfig::ASSIGNMENT_QUEUE_RESERVE);
    m_pendingAssignmentIndex.reserve(AIConfig::ASSIGNMENT_QUEUE_RESERVE);

    // Initialize double buffers
    m_storage.doubleBuffer[0].reserve(INITIAL_CAPACITY);
    m_storage.doubleBuffer[1].reserve(INITIAL_CAPACITY);

    // Initialize lock-free message queue
    for (auto &msg : m_lockFreeMessages) {
      msg.ready.store(false, std::memory_order_relaxed);
    }

    // Configure threading based on system capabilities
    if (HammerEngine::ThreadSystem::Exists()) {
      const auto &threadSystem = HammerEngine::ThreadSystem::Instance();
      m_maxThreads = threadSystem.getThreadCount();
      m_useThreading.store(m_maxThreads > 1, std::memory_order_release);
    }

    m_initialized.store(true, std::memory_order_release);
    m_isShutdown = false;

    AI_LOG("AIManager initialized successfully");
    return true;

  } catch (const std::exception &e) {
    AI_ERROR("Failed to initialize AIManager: " + std::string(e.what()));
    return false;
  }
}

void AIManager::clean() {
  if (m_isShutdown) {
    return;
  }

  AI_LOG("AIManager shutting down...");

  // Mark as shutting down
  m_isShutdown = true;
  m_initialized.store(false, std::memory_order_release);

  // Stop accepting new tasks
  m_globallyPaused.store(true, std::memory_order_release);

  // Clean up all entities and behaviors
  {
    std::unique_lock<std::shared_mutex> entitiesLock(m_entitiesMutex);
    std::unique_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);
    std::lock_guard<std::mutex> assignmentsLock(m_assignmentsMutex);
    std::lock_guard<std::mutex> messagesLock(m_messagesMutex);

    // Clear all storage
    m_storage.hotData.clear();
    m_storage.entities.clear();
    m_storage.behaviors.clear();
    m_storage.lastUpdateTimes.clear();
    m_storage.doubleBuffer[0].clear();
    m_storage.doubleBuffer[1].clear();

    m_entityToIndex.clear();
    m_behaviorTemplates.clear();
    m_pendingAssignments.clear();
    m_pendingAssignmentIndex.clear();
    m_messageQueue.clear();
  }

  // Reset all counters
  m_totalBehaviorExecutions.store(0, std::memory_order_relaxed);
  m_totalAssignmentCount.store(0, std::memory_order_relaxed);
  m_frameCounter.store(0, std::memory_order_relaxed);

  AI_LOG("AIManager shutdown complete");
}

void AIManager::prepareForStateTransition() {
  AI_LOG("Preparing AIManager for state transition...");

  // Pause AI processing to prevent new tasks
  m_globallyPaused.store(true, std::memory_order_release);

  // Process any pending messages
  processMessageQueue();

  // Clean up all entities safely
  cleanupAllEntities();

  // Clear managed entities list
  {
    std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);
    m_managedEntities.clear();
  }

  // Reset behaviors
  resetBehaviors();

  // Reset pause state to false so next state starts unpaused
  m_globallyPaused.store(false, std::memory_order_release);

  AI_LOG("AIManager prepared for state transition");
}

void AIManager::update([[maybe_unused]] float deltaTime) {
  if (!m_initialized.load(std::memory_order_acquire) ||
      m_globallyPaused.load(std::memory_order_acquire)) {
    return;
  }

  auto startTime = std::chrono::high_resolution_clock::now();

  try {
    // Process pending assignments
    processPendingBehaviorAssignments();

    // Get entity count without lock
    size_t entityCount = m_storage.size();
    if (entityCount == 0)
      return;

    // Lock-free double buffer swap
    int currentBuffer = m_storage.currentBuffer.load(std::memory_order_acquire);
    int nextBuffer = 1 - currentBuffer;

    // Get player position for distance calculations (only every 4th frame to
    // reduce CPU usage)
    EntityPtr player = m_playerEntity.lock();
    bool distancesUpdated = false;
    uint64_t currentFrame = m_frameCounter.load(std::memory_order_relaxed);
    if (player && (currentFrame % 4 == 0)) {
      Vector2D playerPos = player->getPosition();

      // Simple scalar distance updates (more efficient for scattered memory
      // access)
      updateDistancesScalar(playerPos);
      distancesUpdated = true;
    }

    // Copy current state to next buffer only if we updated distances or have
    // changes
    bool entityCountChanged =
        (m_storage.doubleBuffer[nextBuffer].size() != m_storage.hotData.size());

    if (distancesUpdated || currentFrame % 60 == 0 || entityCountChanged) {
      std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
      m_storage.doubleBuffer[nextBuffer] = m_storage.hotData;
    } else {
      // Use current buffer data
      nextBuffer = currentBuffer;
    }

    // Determine threading strategy
    bool useThreading = (entityCount >= THREADING_THRESHOLD &&
                         m_useThreading.load(std::memory_order_acquire) &&
                         HammerEngine::ThreadSystem::Exists());

    if (useThreading) {
      auto &threadSystem = HammerEngine::ThreadSystem::Instance();
      size_t availableWorkers =
          static_cast<size_t>(threadSystem.getThreadCount());

      // Check queue pressure before submitting tasks
      size_t queueSize = threadSystem.getQueueSize();
      size_t queueCapacity = threadSystem.getQueueCapacity();
      size_t pressureThreshold =
          (queueCapacity * 9) / 10; // 90% capacity threshold

      if (queueSize > pressureThreshold) {
        // Graceful degradation: fallback to single-threaded processing
        AI_DEBUG("Queue pressure detected (" + std::to_string(queueSize) + "/" +
                 std::to_string(queueCapacity) +
                 "), using single-threaded processing");
        processBatch(0, entityCount, deltaTime, nextBuffer);

        // Swap buffers atomically
        if (nextBuffer != currentBuffer) {
          m_storage.currentBuffer.store(nextBuffer, std::memory_order_release);
        }

        // Process message queue and continue with performance tracking
        processMessageQueue();

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration =
            std::chrono::duration<double, std::milli>(endTime - startTime)
                .count();
        currentFrame = m_frameCounter.fetch_add(1, std::memory_order_relaxed);

        // Periodic cleanup and stats
        if (currentFrame % 300 == 0) {
          cleanupInactiveEntities();
          m_lastCleanupFrame.store(currentFrame, std::memory_order_relaxed);

          std::lock_guard<std::mutex> statsLock(m_statsMutex);
          m_globalStats.addSample(duration, entityCount);

          if (entityCount > 0) {
            double avgDuration = m_globalStats.updateCount > 0
                                     ? (m_globalStats.totalUpdateTime /
                                        m_globalStats.updateCount)
                                     : 0.0;
            AI_DEBUG("AI Summary - Entities: " + std::to_string(entityCount) +
                     ", Avg Update: " + std::to_string(avgDuration) + "ms" +
                     ", Entities/sec: " +
                     std::to_string(m_globalStats.entitiesPerSecond));
          }
        }
        return; // Exit early after single-threaded processing
      }

      HammerEngine::WorkerBudget budget =
          HammerEngine::calculateWorkerBudget(availableWorkers);

      // Use WorkerBudget system properly with threshold-based buffer allocation
      size_t optimalWorkerCount =
          budget.getOptimalWorkerCount(budget.aiAllocated, entityCount, 1000);

      // Dynamic batch sizing based on queue pressure for optimal performance
      size_t minEntitiesPerBatch = 1000;
      size_t maxBatches = 4;

      // Adjust batch strategy based on queue pressure
      double queuePressure = static_cast<double>(queueSize) / queueCapacity;
      if (queuePressure > 0.5) {
        // High pressure: use fewer, larger batches to reduce queue overhead
        minEntitiesPerBatch = 1500;
        maxBatches = 2;
        AI_DEBUG("High queue pressure (" +
                 std::to_string(static_cast<int>(queuePressure * 100)) +
                 "%), using larger batches");
      } else if (queuePressure < 0.25) {
        // Low pressure: can use more batches for better parallelization
        minEntitiesPerBatch = 800;
        maxBatches = 4;
      }

      size_t batchCount =
          std::min(optimalWorkerCount, entityCount / minEntitiesPerBatch);
      batchCount = std::max(size_t(1), std::min(batchCount, maxBatches));

      size_t entitiesPerBatch = entityCount / batchCount;
      size_t remainingEntities = entityCount % batchCount;

      // Submit optimized batches
      for (size_t i = 0; i < batchCount; ++i) {
        size_t start = i * entitiesPerBatch;
        size_t end = start + entitiesPerBatch;

        // Add remaining entities to last batch
        if (i == batchCount - 1) {
          end += remainingEntities;
        }

        threadSystem.enqueueTask(
            [this, start, end, deltaTime, nextBuffer]() {
              processBatch(start, end, deltaTime, nextBuffer);
            },
            HammerEngine::TaskPriority::High, "AI_OptimalBatch");
      }

    } else {
      // Single-threaded processing
      processBatch(0, entityCount, deltaTime, nextBuffer);
    }

    // Swap buffers atomically only if we actually changed buffers
    if (nextBuffer != currentBuffer) {
      m_storage.currentBuffer.store(nextBuffer, std::memory_order_release);
    }

    // Process lock-free message queue
    processMessageQueue();

    // Performance tracking
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration<double, std::milli>(endTime - startTime).count();

    currentFrame = m_frameCounter.fetch_add(1, std::memory_order_relaxed);

    // Periodic cleanup and stats (balanced frequency)
    if (currentFrame % 300 == 0) {
      cleanupInactiveEntities();
      m_lastCleanupFrame.store(currentFrame, std::memory_order_relaxed);

      std::lock_guard<std::mutex> statsLock(m_statsMutex);
      m_globalStats.addSample(duration, entityCount);

      if (entityCount > 0) {
        double avgDuration =
            m_globalStats.updateCount > 0
                ? (m_globalStats.totalUpdateTime / m_globalStats.updateCount)
                : 0.0;
        AI_DEBUG("AI Summary - Entities: " + std::to_string(entityCount) +
                 ", Avg Update: " + std::to_string(avgDuration) + "ms" +
                 ", Entities/sec: " +
                 std::to_string(m_globalStats.entitiesPerSecond));
      }
    }

  } catch (const std::exception &e) {
    AI_ERROR("Exception in AIManager::update: " + std::string(e.what()));
  }
}

void AIManager::registerBehavior(const std::string &name,
                                 std::shared_ptr<AIBehavior> behavior) {
  if (!behavior) {
    AI_ERROR("Attempted to register null behavior with name: " + name);
    return;
  }

  std::unique_lock<std::shared_mutex> lock(m_behaviorsMutex);
  m_behaviorTemplates[name] = behavior;
  AI_LOG("Registered behavior: " + name);
}

bool AIManager::hasBehavior(const std::string &name) const {
  std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
  return m_behaviorTemplates.find(name) != m_behaviorTemplates.end();
}

std::shared_ptr<AIBehavior>
AIManager::getBehavior(const std::string &name) const {
  std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
  auto it = m_behaviorTemplates.find(name);
  return (it != m_behaviorTemplates.end()) ? it->second : nullptr;
}

void AIManager::assignBehaviorToEntity(EntityPtr entity,
                                       const std::string &behaviorName) {
  if (!entity) {
    AI_ERROR("Cannot assign behavior to null entity");
    return;
  }

  // Get behavior template
  std::shared_ptr<AIBehavior> behaviorTemplate;
  {
    std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
    auto it = m_behaviorTemplates.find(behaviorName);
    if (it == m_behaviorTemplates.end()) {
      AI_ERROR("Behavior not found: " + behaviorName);
      return;
    }
    behaviorTemplate = it->second;
  }

  // Clone behavior for this entity
  auto behavior = behaviorTemplate->clone();
  behavior->init(entity);

  // Find or create entity entry
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  auto indexIt = m_entityToIndex.find(entity);
  if (indexIt != m_entityToIndex.end()) {
    // Update existing entity
    size_t index = indexIt->second;
    if (index < m_storage.size()) {
      // Clean up old behavior
      if (m_storage.behaviors[index]) {
        m_storage.behaviors[index]->clean(entity);
      }

      // Assign new behavior
      m_storage.behaviors[index] = behavior;
      m_storage.hotData[index].behaviorType =
          static_cast<uint8_t>(inferBehaviorType(behaviorName));
      m_storage.hotData[index].active = true;

      AI_LOG("Updated behavior for existing entity to: " + behaviorName);
    }
  } else {
    // Add new entity
    size_t newIndex = m_storage.size();

    // Add to hot data
    AIEntityData::HotData hotData{};
    hotData.position = entity->getPosition();
    hotData.lastPosition = hotData.position;
    hotData.distanceSquared = 0.0f;
    hotData.frameCounter = 0;
    hotData.priority = DEFAULT_PRIORITY;
    hotData.behaviorType =
        static_cast<uint8_t>(inferBehaviorType(behaviorName));
    hotData.active = true;
    hotData.shouldUpdate = true;

    m_storage.hotData.push_back(hotData);
    m_storage.entities.push_back(entity);
    m_storage.behaviors.push_back(behavior);
    m_storage.lastUpdateTimes.push_back(0.0f);

    // Update index map
    m_entityToIndex[entity] = newIndex;

    AI_LOG("Added new entity with behavior: " + behaviorName);
  }

  m_totalAssignmentCount.fetch_add(1, std::memory_order_relaxed);
}

void AIManager::unassignBehaviorFromEntity(EntityPtr entity) {
  if (!entity)
    return;

  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  auto it = m_entityToIndex.find(entity);
  if (it != m_entityToIndex.end()) {
    size_t index = it->second;
    if (index < m_storage.size()) {
      m_storage.hotData[index].active = false;

      if (m_storage.behaviors[index]) {
        m_storage.behaviors[index]->clean(entity);
      }
    }
  }
}

bool AIManager::entityHasBehavior(EntityPtr entity) const {
  if (!entity)
    return false;

  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);

  auto it = m_entityToIndex.find(entity);
  if (it != m_entityToIndex.end() && it->second < m_storage.size()) {
    return m_storage.hotData[it->second].active &&
           m_storage.behaviors[it->second] != nullptr;
  }

  return false;
}

void AIManager::queueBehaviorAssignment(EntityPtr entity,
                                        const std::string &behaviorName) {
  if (!entity || behaviorName.empty()) {
    AI_ERROR("Invalid behavior assignment request");
    return;
  }

  std::lock_guard<std::mutex> lock(m_assignmentsMutex);

  // Check for duplicate assignments
  auto it = m_pendingAssignmentIndex.find(entity);
  if (it != m_pendingAssignmentIndex.end()) {
    // Update existing assignment
    it->second = behaviorName;
  } else {
    // Add new assignment
    m_pendingAssignments.emplace_back(entity, behaviorName);
    m_pendingAssignmentIndex[entity] = behaviorName;
  }
}

size_t AIManager::processPendingBehaviorAssignments() {
  std::vector<PendingAssignment> toProcess;
  {
    std::lock_guard<std::mutex> lock(m_assignmentsMutex);
    if (m_pendingAssignments.empty()) {
      return 0;
    }
    // Move all pending assignments to local vector
    toProcess = std::move(m_pendingAssignments);
    m_pendingAssignments.clear();
    m_pendingAssignmentIndex.clear();
  }

  size_t processed = 0;
  size_t assignmentCount = toProcess.size();

  // Threaded batch assignment using WorkerBudget
  bool useThreading = m_useThreading.load(std::memory_order_acquire) &&
                      HammerEngine::ThreadSystem::Exists();
  if (useThreading && assignmentCount > 1000) {
    auto &threadSystem = HammerEngine::ThreadSystem::Instance();
    size_t availableWorkers =
        static_cast<size_t>(threadSystem.getThreadCount());
    size_t queueSize = threadSystem.getQueueSize();
    size_t queueCapacity = threadSystem.getQueueCapacity();
    double queuePressure = static_cast<double>(queueSize) / queueCapacity;

    HammerEngine::WorkerBudget budget =
        HammerEngine::calculateWorkerBudget(availableWorkers);
    size_t optimalWorkerCount =
        budget.getOptimalWorkerCount(budget.aiAllocated, assignmentCount, 1000);

    size_t minAssignmentsPerBatch = 1000;
    size_t maxBatches = 4;
    if (queuePressure > 0.5) {
      minAssignmentsPerBatch = 1500;
      maxBatches = 2;
    } else if (queuePressure < 0.25) {
      minAssignmentsPerBatch = 800;
      maxBatches = 4;
    }
    size_t batchCount =
        std::min(optimalWorkerCount, assignmentCount / minAssignmentsPerBatch);
    batchCount = std::max(size_t(1), std::min(batchCount, maxBatches));
    size_t assignmentsPerBatch = assignmentCount / batchCount;
    size_t remaining = assignmentCount % batchCount;

    std::vector<std::future<void>> futures;
    size_t start = 0;
    for (size_t i = 0; i < batchCount; ++i) {
      size_t end =
          start + assignmentsPerBatch + (i == batchCount - 1 ? remaining : 0);
      futures.push_back(threadSystem.enqueueTaskWithResult(
          [this, &toProcess, start, end]() {
            for (size_t j = start; j < end && j < toProcess.size(); ++j) {
              if (toProcess[j].entity) {
                assignBehaviorToEntity(toProcess[j].entity,
                                       toProcess[j].behaviorName);
              }
            }
          },
          HammerEngine::TaskPriority::High, "AI_AssignmentBatch"));
      start = end;
    }
    // Wait for all assignment batches to complete
    for (auto &f : futures)
      f.get();
    processed = assignmentCount;
  } else {
    // Single-threaded fallback
    for (const auto &assignment : toProcess) {
      if (assignment.entity) {
        assignBehaviorToEntity(assignment.entity, assignment.behaviorName);
        processed++;
      }
    }
  }
  return processed;
}

void AIManager::setPlayerForDistanceOptimization(EntityPtr player) {
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);
  m_playerEntity = player;
}

EntityPtr AIManager::getPlayerReference() const {
  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
  return m_playerEntity.lock();
}

Vector2D AIManager::getPlayerPosition() const {
  if (auto player = getPlayerReference()) {
    return player->getPosition();
  }
  return Vector2D{0.0f, 0.0f};
}

bool AIManager::isPlayerValid() const {
  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
  return !m_playerEntity.expired();
}

void AIManager::registerEntityForUpdates(EntityPtr entity, int priority) {
  if (!entity) {
    AI_ERROR("Cannot register null entity for updates");
    return;
  }

  // Clamp priority to valid range
  priority = std::max(AI_MIN_PRIORITY, std::min(AI_MAX_PRIORITY, priority));

  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Check if entity already exists
  auto it = m_entityToIndex.find(entity);
  if (it != m_entityToIndex.end()) {
    // Update priority for existing entity
    size_t index = it->second;
    if (index < m_storage.size()) {
      m_storage.hotData[index].priority = static_cast<uint8_t>(priority);
    }
  } else {
    // Add managed entity info
    EntityUpdateInfo info;
    info.entityWeak = entity;
    info.priority = priority;
    info.frameCounter = 0;
    info.lastUpdateTime = getCurrentTimeNanos();

    m_managedEntities.push_back(info);
  }
}

void AIManager::unregisterEntityFromUpdates(EntityPtr entity) {
  if (!entity)
    return;

  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Remove from managed entities
  m_managedEntities.erase(
      std::remove_if(m_managedEntities.begin(), m_managedEntities.end(),
                     [&entity](const EntityUpdateInfo &info) {
                       auto e = info.entityWeak.lock();
                       return !e || e == entity;
                     }),
      m_managedEntities.end());

  // Mark as inactive in main storage
  auto it = m_entityToIndex.find(entity);
  if (it != m_entityToIndex.end() && it->second < m_storage.size()) {
    m_storage.hotData[it->second].active = false;
  }
}

void AIManager::setGlobalPause(bool paused) {
  m_globallyPaused.store(paused, std::memory_order_release);
  AI_LOG((paused ? "AI processing paused" : "AI processing resumed"));
}

bool AIManager::isGloballyPaused() const {
  return m_globallyPaused.load(std::memory_order_acquire);
}

void AIManager::resetBehaviors() {
  AI_LOG("Resetting all AI behaviors");

  std::unique_lock<std::shared_mutex> entitiesLock(m_entitiesMutex);
  std::unique_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);

  // Clean up all behaviors
  for (size_t i = 0; i < m_storage.size(); ++i) {
    if (m_storage.behaviors[i] && m_storage.entities[i]) {
      m_storage.behaviors[i]->clean(m_storage.entities[i]);
    }
  }

  // Clear all data
  m_storage.hotData.clear();
  m_storage.entities.clear();
  m_storage.behaviors.clear();
  m_storage.lastUpdateTimes.clear();
  m_entityToIndex.clear();
  m_managedEntities.clear();

  // Clear double buffers to prevent stale data synchronization issues
  m_storage.doubleBuffer[0].clear();
  m_storage.doubleBuffer[1].clear();
  m_storage.currentBuffer.store(0, std::memory_order_release);

  // Reset counters
  m_totalBehaviorExecutions.store(0, std::memory_order_relaxed);
}

void AIManager::configureThreading(bool useThreading, unsigned int maxThreads) {
  m_useThreading.store(useThreading, std::memory_order_release);

  if (maxThreads > 0) {
    m_maxThreads = maxThreads;
  }

  AI_LOG("Threading configured: " +
         std::string(useThreading ? "enabled" : "disabled") +
         " with max threads: " + std::to_string(m_maxThreads));
}

void AIManager::configurePriorityMultiplier(float multiplier) {
  m_priorityMultiplier.store(multiplier, std::memory_order_release);
}

AIPerformanceStats AIManager::getPerformanceStats() const {
  std::lock_guard<std::mutex> lock(m_statsMutex);
  return m_globalStats;
}

size_t AIManager::getBehaviorCount() const {
  std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
  return m_behaviorTemplates.size();
}

size_t AIManager::getManagedEntityCount() const {
  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Count only active entities
  size_t activeCount = 0;
  for (size_t i = 0; i < m_storage.size(); ++i) {
    if (m_storage.hotData[i].active) {
      activeCount++;
    }
  }

  return activeCount;
}

size_t AIManager::getBehaviorUpdateCount() const {
  return m_totalBehaviorExecutions.load(std::memory_order_relaxed);
}

size_t AIManager::getTotalAssignmentCount() const {
  return m_totalAssignmentCount.load(std::memory_order_relaxed);
}

void AIManager::sendMessageToEntity(EntityPtr entity,
                                    const std::string &message,
                                    bool immediate) {
  if (!entity || message.empty())
    return;

  if (immediate) {
    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
    auto it = m_entityToIndex.find(entity);
    if (it != m_entityToIndex.end() && it->second < m_storage.size()) {
      if (m_storage.behaviors[it->second]) {
        m_storage.behaviors[it->second]->onMessage(entity, message);
      }
    }
  } else {
    // Use lock-free queue for non-immediate messages
    size_t writeIndex =
        m_messageWriteIndex.fetch_add(1, std::memory_order_relaxed) %
        MESSAGE_QUEUE_SIZE;
    auto &msg = m_lockFreeMessages[writeIndex];

    msg.target = entity;
    std::strncpy(msg.message, message.c_str(), sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';
    msg.ready.store(true, std::memory_order_release);
  }
}

void AIManager::broadcastMessage(const std::string &message, bool immediate) {
  if (message.empty())
    return;

  if (immediate) {
    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);

    for (size_t i = 0; i < m_storage.size(); ++i) {
      if (m_storage.hotData[i].active && m_storage.behaviors[i]) {
        m_storage.behaviors[i]->onMessage(m_storage.entities[i], message);
      }
    }
  } else {
    // Queue broadcast for processing in next update
    size_t writeIndex =
        m_messageWriteIndex.fetch_add(1, std::memory_order_relaxed) %
        MESSAGE_QUEUE_SIZE;
    auto &msg = m_lockFreeMessages[writeIndex];

    msg.target.reset(); // No specific target for broadcast
    std::strncpy(msg.message, message.c_str(), sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';
    msg.ready.store(true, std::memory_order_release);
  }
}

void AIManager::processMessageQueue() {
  // Process lock-free message queue
  size_t readIndex = m_messageReadIndex.load(std::memory_order_relaxed);
  size_t writeIndex = m_messageWriteIndex.load(std::memory_order_acquire);

  while (readIndex != writeIndex) {
    auto &msg = m_lockFreeMessages[readIndex % MESSAGE_QUEUE_SIZE];

    if (msg.ready.load(std::memory_order_acquire)) {
      if (auto entity = msg.target.lock()) {
        sendMessageToEntity(entity, msg.message, true);
      }

      msg.ready.store(false, std::memory_order_release);
    }

    readIndex++;
  }

  m_messageReadIndex.store(readIndex, std::memory_order_release);
}

BehaviorType
AIManager::inferBehaviorType(const std::string &behaviorName) const {
  auto it = m_behaviorTypeMap.find(behaviorName);
  return (it != m_behaviorTypeMap.end()) ? it->second : BehaviorType::Custom;
}

void AIManager::processBatch(size_t start, size_t end, float deltaTime,
                             int bufferIndex) {
  // Work on the double buffer for lock-free operation
  auto &workBuffer = m_storage.doubleBuffer[bufferIndex];

  // Get current player position
  EntityPtr player = m_playerEntity.lock();

  size_t batchExecutions = 0;

  // Pre-calculate common values once per batch to reduce per-entity overhead
  float maxDist = m_maxUpdateDistance.load(std::memory_order_relaxed);
  float maxDistSquared = maxDist * maxDist;
  bool hasPlayer = (player != nullptr);

  // Pre-cache entities and behaviors for the entire batch to reduce lock
  // contention
  std::vector<EntityPtr> batchEntities;
  std::vector<std::shared_ptr<AIBehavior>> batchBehaviors;
  batchEntities.reserve(end - start);
  batchBehaviors.reserve(end - start);

  // Single lock acquisition for the entire batch
  {
    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
    for (size_t i = start; i < end && i < m_storage.size(); ++i) {
      batchEntities.push_back(m_storage.entities[i]);
      batchBehaviors.push_back(m_storage.behaviors[i]);
    }
  }

  // Process entities without locks
  for (size_t idx = 0; idx < batchEntities.size(); ++idx) {
    size_t i = start + idx;
    if (i >= workBuffer.size())
      break;

    auto &hotData = workBuffer[i];
    if (!hotData.active)
      continue;

    EntityPtr entity = batchEntities[idx];
    std::shared_ptr<AIBehavior> behavior = batchBehaviors[idx];

    if (!entity || !behavior) {
      hotData.active = false;
      continue;
    }

    try {
      // Update position
      hotData.position = entity->getPosition();

      // Simple distance-based culling - no frame counting needed
      bool shouldUpdate = true;
      if (hasPlayer) {
        // Use pre-calculated values from batch level
        float priorityMultiplier = 1.0f + hotData.priority * 0.1f;
        float effectiveMaxDistSquared =
            maxDistSquared * priorityMultiplier * priorityMultiplier;

        // Pure distance-based culling - entities too far away don't update
        shouldUpdate = (hotData.distanceSquared <= effectiveMaxDistSquared);
      }

      if (shouldUpdate) {
        // Execute behavior with staggering support
        uint64_t currentFrame = m_frameCounter.load(std::memory_order_relaxed);
        behavior->executeLogicWithStaggering(entity, currentFrame);
        batchExecutions++;

        // Update entity
        entity->update(deltaTime);

        // Update position after behavior execution
        hotData.lastPosition = hotData.position;
        hotData.position = entity->getPosition();
      }

    } catch (const std::exception &e) {
      AI_ERROR("Error in batch processing: " + std::string(e.what()));
      hotData.active = false;
    }
  }

  if (batchExecutions > 0) {
    m_totalBehaviorExecutions.fetch_add(batchExecutions,
                                        std::memory_order_relaxed);
  }
}

void AIManager::updateDistancesScalar(const Vector2D &playerPos) {
  size_t entityCount = m_storage.hotData.size();

  // Simple scalar implementation - only update active entities
  // Skip inactive entities to reduce CPU usage significantly
  size_t updatedCount = 0;
  for (size_t i = 0; i < entityCount; ++i) {
    auto &hotData = m_storage.hotData[i];
    if (hotData.active) {
      Vector2D diff = hotData.position - playerPos;
      hotData.distanceSquared = diff.lengthSquared();
      updatedCount++;
    }
  }

  // Optional: Early exit if no entities were active
  if (updatedCount == 0) {
    return;
  }
}

void AIManager::cleanupInactiveEntities() {
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Find all inactive entities
  std::vector<size_t> toRemove;
  for (size_t i = 0; i < m_storage.size(); ++i) {
    if (!m_storage.hotData[i].active) {
      toRemove.push_back(i);
    }
  }

  // Remove in reverse order to maintain indices
  for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it) {
    size_t index = *it;

    // Remove from entity map
    if (index < m_storage.entities.size()) {
      m_entityToIndex.erase(m_storage.entities[index]);
    }

    // Swap with last element and pop
    if (index < m_storage.size() - 1) {
      size_t lastIndex = m_storage.size() - 1;

      // Update hot data
      m_storage.hotData[index] = m_storage.hotData[lastIndex];

      // Update cold data
      m_storage.entities[index] = m_storage.entities[lastIndex];
      m_storage.behaviors[index] = m_storage.behaviors[lastIndex];
      m_storage.lastUpdateTimes[index] = m_storage.lastUpdateTimes[lastIndex];

      // Update index map
      m_entityToIndex[m_storage.entities[index]] = index;
    }

    // Remove last element
    m_storage.hotData.pop_back();
    m_storage.entities.pop_back();
    m_storage.behaviors.pop_back();
    m_storage.lastUpdateTimes.pop_back();
  }

  AI_DEBUG("Cleaned up " + std::to_string(toRemove.size()) +
           " inactive entities");
}

void AIManager::cleanupAllEntities() {
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Clean all behaviors
  for (size_t i = 0; i < m_storage.size(); ++i) {
    if (m_storage.behaviors[i] && m_storage.entities[i]) {
      try {
        m_storage.behaviors[i]->clean(m_storage.entities[i]);
      } catch (const std::exception &e) {
        AI_ERROR("Exception cleaning behavior: " + std::string(e.what()));
      }
    }
  }

  // Clear all storage
  m_storage.hotData.clear();
  m_storage.entities.clear();
  m_storage.behaviors.clear();
  m_storage.lastUpdateTimes.clear();
  m_entityToIndex.clear();

  AI_DEBUG("Cleaned up all entities for state transition");
}

void AIManager::recordPerformance(BehaviorType type, double timeMs,
                                  uint64_t entities) {
  std::lock_guard<std::mutex> lock(m_statsMutex);

  size_t typeIndex = static_cast<size_t>(type);
  if (typeIndex < m_behaviorStats.size()) {
    m_behaviorStats[typeIndex].addSample(timeMs, entities);
  }
}

uint64_t AIManager::getCurrentTimeNanos() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

int AIManager::getEntityPriority(EntityPtr entity) const {
  if (!entity)
    return DEFAULT_PRIORITY;

  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
  auto it = m_entityToIndex.find(entity);
  if (it != m_entityToIndex.end() && it->second < m_storage.size()) {
    return m_storage.hotData[it->second].priority;
  }
  return DEFAULT_PRIORITY;
}

float AIManager::getUpdateRangeMultiplier(int priority) const {
  // Higher priority = larger update range multiplier
  return 1.0f + (std::max(0, std::min(9, priority)) * 0.1f);
}

void AIManager::registerEntityForUpdates(EntityPtr entity, int priority,
                                         const std::string &behaviorName) {
  // Register for updates
  registerEntityForUpdates(entity, priority);

  // Queue behavior assignment
  queueBehaviorAssignment(entity, behaviorName);
}
