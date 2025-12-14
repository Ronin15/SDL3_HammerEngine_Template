/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/WorldResourceManager.hpp"
#include "core/Logger.hpp"
#include "events/WorldEvent.hpp"
#include "managers/EventManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include <algorithm>
#include <cassert>
#include <format>

WorldResourceManager &WorldResourceManager::Instance() {
  static WorldResourceManager instance;
  return instance;
}

WorldResourceManager::~WorldResourceManager() {
  // Only clean up if not already shut down
  if (!m_isShutdown) {
    clean();
  }
}

bool WorldResourceManager::init() {
  if (m_initialized.load(std::memory_order_acquire)) {
    return true; // Already initialized
  }

  std::lock_guard<std::shared_mutex> lock(m_resourceMutex);

  if (m_initialized.load(std::memory_order_acquire)) {
    return true; // Double-check after acquiring lock
  }

  try {
    // Clear any existing data
    m_worldResources.clear();

    // Create a default world for single-world scenarios
    m_worldResources["default"] =
        std::unordered_map<HammerEngine::ResourceHandle, Quantity>();

    // Register event handlers for world events
    registerEventHandlers();

    m_initialized.store(true, std::memory_order_release);
    m_isShutdown = false; // Explicitly mark as not shut down
    m_stats.reset();
    m_stats.worldsTracked = 1; // Default world

    WORLD_RESOURCE_INFO("WorldResourceManager initialized with default world");
    return true;
  } catch (const std::exception &ex) {
    WORLD_RESOURCE_ERROR("WorldResourceManager::init - Exception: " +
                         std::string(ex.what()));
    return false;
  }
}

void WorldResourceManager::clean() {
  if (!m_initialized.load(std::memory_order_acquire) || m_isShutdown) {
    return; // Already shut down or not initialized
  }

  // Unregister event handlers before cleanup
  unregisterEventHandlers();

  std::lock_guard<std::shared_mutex> lock(m_resourceMutex);
  std::lock_guard<std::mutex> cacheLock(m_cacheMutex);

  // Clear all data structures
  m_worldResources.clear();
  m_resourceCache.clear();
  m_aggregateCache = ResourceAggregateCache{}; // Reset aggregate cache

  m_initialized.store(false, std::memory_order_release);
  m_isShutdown = true;
  m_stats.reset();

  WORLD_RESOURCE_INFO("WorldResourceManager cleaned up");
}

bool WorldResourceManager::configure(const WorldResourceManagerConfig &config) {
  if (!config.isValid()) {
    WORLD_RESOURCE_ERROR(
        "WorldResourceManager::configure - Invalid configuration");
    return false;
  }

  std::lock_guard<std::shared_mutex> lock(m_resourceMutex);
  std::lock_guard<std::mutex> cacheLock(m_cacheMutex);

  // Store old config for comparison
  auto oldConfig = m_config;
  m_config = config;

  // If cache size changed, we need to resize existing caches
  if (oldConfig.perWorldCacheSize != config.perWorldCacheSize) {
    for (auto &[worldId, worldCache] : m_resourceCache) {
      if (worldCache.size() > config.perWorldCacheSize) {
        // Trim cache to new size, keeping most recently accessed entries
        std::sort(worldCache.begin(), worldCache.end(),
                  [](const ResourceCache &a, const ResourceCache &b) {
                    return a.lastAccess > b.lastAccess; // Most recent first
                  });
        worldCache.resize(config.perWorldCacheSize);
        m_stats.cacheEvictions +=
            (worldCache.size() - config.perWorldCacheSize);
      }
    }
  }

  // If cache expiry time changed, invalidate aggregate cache
  if (oldConfig.cacheExpiryTime != config.cacheExpiryTime) {
    m_aggregateCache.valid = false;
  }

  WORLD_RESOURCE_INFO(std::format(
      "WorldResourceManager configuration updated - CacheSize: {}, ExpiryTime: {}ms",
      config.perWorldCacheSize, config.cacheExpiryTime.count()));
  return true;
}

WorldResourceManagerConfig WorldResourceManager::getConfig() const {
  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);
  return m_config;
}

bool WorldResourceManager::createWorld(const WorldId &worldId) {
  if (!isValidWorldId(worldId)) {
    WORLD_RESOURCE_ERROR(std::format(
        "WorldResourceManager::createWorld - Invalid world ID: {}", worldId));
    return false;
  }

  std::lock_guard<std::shared_mutex> lock(m_resourceMutex);

  if (m_worldResources.find(worldId) != m_worldResources.end()) {
    WORLD_RESOURCE_WARN(std::format(
        "WorldResourceManager::createWorld - World already exists: {}", worldId));
    return false;
  }

  try {
    m_worldResources[worldId] =
        std::unordered_map<HammerEngine::ResourceHandle, Quantity>();
    m_stats.worldsTracked.fetch_add(1, std::memory_order_relaxed);

    // Check for performance warning
    if (m_stats.worldsTracked.load() > m_config.maxWorldsBeforeWarning) {
      WORLD_RESOURCE_WARN(std::format(
          "WorldResourceManager::createWorld - High world count ({}) may impact performance",
          m_stats.worldsTracked.load()));
    }

    WORLD_RESOURCE_INFO(std::format("Created world: {}", worldId));
    return true;
  } catch (const std::exception &ex) {
    WORLD_RESOURCE_ERROR(std::format("WorldResourceManager::createWorld - Exception: {}",
                                     ex.what()));
    return false;
  }
}

bool WorldResourceManager::removeWorld(const WorldId &worldId) {
  if (!isValidWorldId(worldId)) {
    WORLD_RESOURCE_ERROR(
        std::format("WorldResourceManager::removeWorld - Invalid world ID: {}", worldId));
    return false;
  }

  if (worldId == "default") {
    WORLD_RESOURCE_WARN(
        "WorldResourceManager::removeWorld - Cannot remove default world");
    return false;
  }

  std::lock_guard<std::shared_mutex> lock(m_resourceMutex);

  auto it = m_worldResources.find(worldId);
  if (it == m_worldResources.end()) {
    WORLD_RESOURCE_WARN(
        std::format("WorldResourceManager::removeWorld - World not found: {}", worldId));
    return false;
  }

  try {
    m_worldResources.erase(it);
    m_stats.worldsTracked.fetch_sub(1, std::memory_order_relaxed);

    WORLD_RESOURCE_INFO(std::format("Removed world: {}", worldId));
    return true;
  } catch (const std::exception &ex) {
    WORLD_RESOURCE_ERROR("WorldResourceManager::removeWorld - Exception: " +
                         std::string(ex.what()));
    return false;
  }
}

bool WorldResourceManager::hasWorld(const WorldId &worldId) const {
  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);
  return m_worldResources.find(worldId) != m_worldResources.end();
}

std::vector<WorldResourceManager::WorldId>
WorldResourceManager::getWorldIds() const {
  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);
  std::vector<WorldId> worldIds;
  worldIds.reserve(m_worldResources.size());

  for (const auto &[worldId, resources] : m_worldResources) {
    worldIds.push_back(worldId);
  }

  return worldIds;
}

ResourceTransactionResult WorldResourceManager::addResource(
    const WorldId &worldId, const HammerEngine::ResourceHandle &resourceHandle,
    Quantity quantity) {
  // Validate parameters individually to return appropriate error codes
  if (!isValidWorldId(worldId)) {
    WORLD_RESOURCE_ERROR(std::format("WorldResourceManager - Invalid world ID: {}", worldId));
    return ResourceTransactionResult::InvalidWorldId;
  }

  if (!isValidResourceHandle(resourceHandle)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid resource handle: " +
                         resourceHandle.toString());
    return ResourceTransactionResult::InvalidResourceHandle;
  }

  if (!isValidQuantity(quantity)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid quantity: " +
                         std::to_string(quantity));
    return ResourceTransactionResult::InvalidQuantity;
  }

  if (quantity < 0) {
    WORLD_RESOURCE_WARN(
        "WorldResourceManager::addResource - Invalid quantity: " +
        std::to_string(quantity));
    return ResourceTransactionResult::InvalidQuantity;
  }

  std::lock_guard<std::shared_mutex> lock(m_resourceMutex);

  if (m_worldResources.find(worldId) == m_worldResources.end()) {
    return ResourceTransactionResult::InvalidWorldId;
  }

  try {
    auto &worldResources = m_worldResources.at(worldId);

    // Check for overflow (only if quantity > 0)
    if (quantity > 0) {
      Quantity currentQuantity = worldResources[resourceHandle];
      if (currentQuantity > std::numeric_limits<Quantity>::max() - quantity) {
        WORLD_RESOURCE_ERROR(
            "WorldResourceManager::addResource - Quantity overflow");
        return ResourceTransactionResult::SystemError;
      }
      Quantity newQuantity = currentQuantity + quantity;
      worldResources[resourceHandle] = newQuantity;

      // Fire resource change event
      fireResourceChangeEvent(worldId, resourceHandle, currentQuantity,
                              newQuantity, "added");

      // Invalidate caches when resources change
      invalidateAggregateCache();
      updateResourceCache(worldId, resourceHandle, newQuantity);
    }
    // If quantity is 0, we do nothing but still consider it successful

    updateStats(true, quantity);

    WORLD_RESOURCE_DEBUG(std::format("Added {} {} to world {}",
                         quantity, resourceHandle.toString(), worldId));
    return ResourceTransactionResult::Success;
  } catch (const std::exception &ex) {
    WORLD_RESOURCE_ERROR("WorldResourceManager::addResource - Exception: " +
                         std::string(ex.what()));
    return ResourceTransactionResult::SystemError;
  }
}

ResourceTransactionResult WorldResourceManager::removeResource(
    const WorldId &worldId, const HammerEngine::ResourceHandle &resourceHandle,
    Quantity quantity) {
  // Validate parameters individually to return appropriate error codes
  if (!isValidWorldId(worldId)) {
    WORLD_RESOURCE_ERROR(std::format("WorldResourceManager - Invalid world ID: {}", worldId));
    return ResourceTransactionResult::InvalidWorldId;
  }

  if (!isValidResourceHandle(resourceHandle)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid resource handle: " +
                         resourceHandle.toString());
    return ResourceTransactionResult::InvalidResourceHandle;
  }

  if (!isValidQuantity(quantity)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid quantity: " +
                         std::to_string(quantity));
    return ResourceTransactionResult::InvalidQuantity;
  }

  if (quantity < 0) {
    WORLD_RESOURCE_WARN(
        "WorldResourceManager::removeResource - Invalid quantity: " +
        std::to_string(quantity));
    return ResourceTransactionResult::InvalidQuantity;
  }

  std::lock_guard<std::shared_mutex> lock(m_resourceMutex);

  if (m_worldResources.find(worldId) == m_worldResources.end()) {
    return ResourceTransactionResult::InvalidWorldId;
  }

  try {
    auto &worldResources = m_worldResources.at(worldId);
    Quantity currentQuantity = worldResources[resourceHandle];

    // If quantity is 0, we do nothing but still consider it successful
    if (quantity == 0) {
      WORLD_RESOURCE_DEBUG(std::format("Removed {} {} from world {}",
                           quantity, resourceHandle.toString(), worldId));
      return ResourceTransactionResult::Success;
    }

    if (currentQuantity < quantity) {
      WORLD_RESOURCE_WARN(std::string("WorldResourceManager::removeResource - "
                                      "Insufficient resources. ") +
                          std::format("Current: {}", currentQuantity) +
                          std::format(", Requested: {}", quantity));
      return ResourceTransactionResult::InsufficientResources;
    }

    Quantity newQuantity = currentQuantity - quantity;
    worldResources[resourceHandle] = newQuantity;

    // Fire resource change event
    fireResourceChangeEvent(worldId, resourceHandle, currentQuantity,
                            newQuantity, "removed");

    updateStats(false, quantity);

    // Invalidate caches when resources change
    invalidateAggregateCache();
    updateResourceCache(worldId, resourceHandle, newQuantity);

    WORLD_RESOURCE_DEBUG(std::format("Removed {} {} from world {}",
                         quantity, resourceHandle.toString(), worldId));
    return ResourceTransactionResult::Success;
  } catch (const std::exception &ex) {
    WORLD_RESOURCE_ERROR("WorldResourceManager::removeResource - Exception: " +
                         std::string(ex.what()));
    return ResourceTransactionResult::SystemError;
  }
}

ResourceTransactionResult WorldResourceManager::setResource(
    const WorldId &worldId, const HammerEngine::ResourceHandle &resourceHandle,
    Quantity quantity) {
  // Validate parameters individually to return appropriate error codes
  if (!isValidWorldId(worldId)) {
    WORLD_RESOURCE_ERROR(std::format("WorldResourceManager - Invalid world ID: {}", worldId));
    return ResourceTransactionResult::InvalidWorldId;
  }

  if (!isValidResourceHandle(resourceHandle)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid resource handle: " +
                         resourceHandle.toString());
    return ResourceTransactionResult::InvalidResourceHandle;
  }

  if (!isValidQuantity(quantity)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid quantity: " +
                         std::to_string(quantity));
    return ResourceTransactionResult::InvalidQuantity;
  }

  if (quantity < 0) {
    WORLD_RESOURCE_WARN(
        "WorldResourceManager::setResource - Invalid quantity: " +
        std::to_string(quantity));
    return ResourceTransactionResult::InvalidQuantity;
  }

  std::lock_guard<std::shared_mutex> lock(m_resourceMutex);

  if (m_worldResources.find(worldId) == m_worldResources.end()) {
    return ResourceTransactionResult::InvalidWorldId;
  }

  try {
    auto &worldResources = m_worldResources.at(worldId);
    Quantity oldQuantity = worldResources[resourceHandle];
    worldResources[resourceHandle] = quantity;

    // Update stats based on the net change
    Quantity netChange = quantity - oldQuantity;
    if (netChange != 0) {
      updateStats(netChange > 0, std::abs(netChange));

      // Invalidate caches when resources change
      invalidateAggregateCache();
      updateResourceCache(worldId, resourceHandle, quantity);
    }

    WORLD_RESOURCE_DEBUG(std::format("Set {} to {} in world {}",
                                     resourceHandle.toString(), quantity, worldId));
    return ResourceTransactionResult::Success;
  } catch (const std::exception &ex) {
    WORLD_RESOURCE_ERROR("WorldResourceManager::setResource - Exception: " +
                         std::string(ex.what()));
    return ResourceTransactionResult::SystemError;
  }
}

WorldResourceManager::Quantity WorldResourceManager::getResourceQuantity(
    const WorldId &worldId,
    const HammerEngine::ResourceHandle &resourceHandle) const {
  // Try cache first for performance
  Quantity cachedQuantity = getCachedResourceQuantity(worldId, resourceHandle);
  if (cachedQuantity >= 0) {
    return cachedQuantity;
  }

  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);

  auto worldIt = m_worldResources.find(worldId);
  if (worldIt == m_worldResources.end()) {
    updateResourceCache(worldId, resourceHandle, 0);
    return 0;
  }

  auto resourceIt = worldIt->second.find(resourceHandle);
  Quantity quantity =
      (resourceIt != worldIt->second.end()) ? resourceIt->second : 0;

  // Update cache for future access
  updateResourceCache(worldId, resourceHandle, quantity);

  return quantity;
}

bool WorldResourceManager::hasResource(
    const WorldId &worldId, const HammerEngine::ResourceHandle &resourceHandle,
    Quantity minimumQuantity) const {
  return getResourceQuantity(worldId, resourceHandle) >= minimumQuantity;
}

WorldResourceManager::Quantity WorldResourceManager::getTotalResourceQuantity(
    const HammerEngine::ResourceHandle &resourceHandle) const {
  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);

  Quantity total = 0;
  for (const auto &[worldId, worldResources] : m_worldResources) {
    auto resourceIt = worldResources.find(resourceHandle);
    if (resourceIt != worldResources.end()) {
      total += resourceIt->second;
    }
  }

  return total;
}

std::unordered_map<HammerEngine::ResourceHandle, WorldResourceManager::Quantity>
WorldResourceManager::getWorldResources(const WorldId &worldId) const {
  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);

  auto worldIt = m_worldResources.find(worldId);
  if (worldIt != m_worldResources.end()) {
    return worldIt->second; // Copy the map
  }

  return {}; // Return empty map if world not found
}

std::unordered_map<HammerEngine::ResourceHandle, WorldResourceManager::Quantity>
WorldResourceManager::getAllResourceTotals() const {
  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);

  // Try to use aggregate cache for performance
  updateAggregateCache();

  {
    std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
    if (m_aggregateCache.valid) {
      return m_aggregateCache.totals; // Return cached copy
    }
  }

  // Fallback to direct calculation if cache invalid
  std::unordered_map<HammerEngine::ResourceHandle, Quantity> totals;

  for (const auto &[worldId, worldResources] : m_worldResources) {
    for (const auto &[resourceHandle, quantity] : worldResources) {
      totals[resourceHandle] += quantity;
    }
  }

  return totals;
}

bool WorldResourceManager::transferResource(
    const WorldId &fromWorldId, const WorldId &toWorldId,
    const HammerEngine::ResourceHandle &resourceHandle, Quantity quantity) {
  if (!validateParameters(fromWorldId, resourceHandle, quantity) ||
      !isValidWorldId(toWorldId)) {
    return false;
  }

  if (quantity <= 0) {
    WORLD_RESOURCE_WARN(
        "WorldResourceManager::transferResource - Invalid quantity: " +
        std::to_string(quantity));
    return false;
  }

  std::lock_guard<std::shared_mutex> lock(m_resourceMutex);

  if (m_worldResources.find(fromWorldId) == m_worldResources.end() ||
      m_worldResources.find(toWorldId) == m_worldResources.end()) {
    return false;
  }

  try {
    auto &fromResources = m_worldResources.at(fromWorldId);
    auto &toResources = m_worldResources.at(toWorldId);

    Quantity fromQuantity = fromResources[resourceHandle];
    if (fromQuantity < quantity) {
      WORLD_RESOURCE_WARN(
          "WorldResourceManager::transferResource - Insufficient resources");
      return false;
    }

    // Check for overflow in destination
    Quantity toQuantity = toResources[resourceHandle];
    if (toQuantity > std::numeric_limits<Quantity>::max() - quantity) {
      WORLD_RESOURCE_ERROR(
          "WorldResourceManager::transferResource - Quantity overflow");
      return false;
    }

    fromResources[resourceHandle] = fromQuantity - quantity;
    toResources[resourceHandle] = toQuantity + quantity;

    m_stats.totalTransactions.fetch_add(1, std::memory_order_relaxed);

    // Invalidate caches when resources change
    invalidateAggregateCache();
    updateResourceCache(fromWorldId, resourceHandle, fromQuantity - quantity);
    updateResourceCache(toWorldId, resourceHandle, toQuantity + quantity);

    WORLD_RESOURCE_DEBUG(std::format("Transferred {} {} from {} to {}",
                         quantity, resourceHandle.toString(), fromWorldId, toWorldId));
    return true;
  } catch (const std::exception &ex) {
    WORLD_RESOURCE_ERROR(
        "WorldResourceManager::transferResource - Exception: " +
        std::string(ex.what()));
    return false;
  }
}

bool WorldResourceManager::transferAllResources(const WorldId &fromWorldId,
                                                const WorldId &toWorldId) {
  if (!isValidWorldId(fromWorldId) || !isValidWorldId(toWorldId)) {
    return false;
  }

  if (fromWorldId == toWorldId) {
    WORLD_RESOURCE_WARN("WorldResourceManager::transferAllResources - Source "
                        "and destination are the same");
    return false;
  }

  std::lock_guard<std::shared_mutex> lock(m_resourceMutex);

  if (m_worldResources.find(fromWorldId) == m_worldResources.end() ||
      m_worldResources.find(toWorldId) == m_worldResources.end()) {
    return false;
  }

  try {
    auto &fromResources = m_worldResources.at(fromWorldId);
    auto &toResources = m_worldResources.at(toWorldId);

    for (const auto &[resourceHandle, quantity] : fromResources) {
      if (quantity > 0) {
        toResources[resourceHandle] += quantity;
      }
    }

    fromResources.clear();
    m_stats.totalTransactions.fetch_add(1, std::memory_order_relaxed);

    // Invalidate all caches when doing bulk transfer
    invalidateAggregateCache();

    WORLD_RESOURCE_INFO(std::format("Transferred all resources from {} to {}",
                                    fromWorldId, toWorldId));
    return true;
  } catch (const std::exception &ex) {
    WORLD_RESOURCE_ERROR(
        "WorldResourceManager::transferAllResources - Exception: " +
        std::string(ex.what()));
    return false;
  }
}

WorldResourceStats WorldResourceManager::getStats() const { return m_stats; }

size_t WorldResourceManager::getMemoryUsage() const {
  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);

  size_t totalSize = 0;

  // Account for m_worldResources map and nested maps
  for (const auto &[worldId, worldResources] : m_worldResources) {
    totalSize += worldId.size(); // Size of world ID string
    totalSize += sizeof(std::unordered_map<HammerEngine::ResourceHandle,
                                           Quantity>); // World resources map

    for (const auto &[resourceHandle, quantity] : worldResources) {
      totalSize += sizeof(resourceHandle); // Size of actual resource handle
      totalSize += sizeof(quantity);       // Size of actual quantity value
      // Each resource entry also has overhead from hash table bucket
      totalSize += sizeof(std::pair<HammerEngine::ResourceHandle, Quantity>);
    }
  }

  return totalSize;
}

bool WorldResourceManager::isValidWorldId(const WorldId &worldId) const {
  return !worldId.empty() && worldId.length() <= 64 && // Reasonable limit
         std::all_of(worldId.begin(), worldId.end(), [](char c) {
           return std::isalnum(c) || c == '_' || c == '-';
         });
}

bool WorldResourceManager::isValidResourceHandle(
    const HammerEngine::ResourceHandle &resourceHandle) const {
  // First check if the handle itself is valid
  if (!resourceHandle.isValid()) {
    return false;
  }

  // Then check if the template exists in the ResourceTemplateManager
  return ResourceTemplateManager::Instance().getResourceTemplate(
             resourceHandle) != nullptr;
}

bool WorldResourceManager::isValidQuantity(Quantity quantity) const {
  return quantity >= 0 && quantity <= std::numeric_limits<Quantity>::max();
}

void WorldResourceManager::updateStats(bool isAdd, Quantity quantity) {
  (void)quantity; // Avoid unused parameter warning
  m_stats.totalTransactions.fetch_add(1, std::memory_order_relaxed);

  if (isAdd) {
    m_stats.addOperations.fetch_add(1, std::memory_order_relaxed);
  } else {
    m_stats.removeOperations.fetch_add(1, std::memory_order_relaxed);
  }

  // Note: We don't update totalResourcesTracked here as it would double-count
  // This stat should represent unique resource types, not quantities
}

bool WorldResourceManager::validateParameters(
    const WorldId &worldId, const HammerEngine::ResourceHandle &resourceHandle,
    Quantity quantity) const {
  if (!isValidWorldId(worldId)) {
    WORLD_RESOURCE_ERROR(std::format("WorldResourceManager - Invalid world ID: {}", worldId));
    return false;
  }

  if (!isValidResourceHandle(resourceHandle)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid resource handle: " +
                         resourceHandle.toString());
    return false;
  }

  if (!isValidQuantity(quantity)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid quantity: " +
                         std::to_string(quantity));
    return false;
  }

  return true;
}

// Cache management methods
void WorldResourceManager::updateResourceCache(
    const WorldId &worldId, const HammerEngine::ResourceHandle &resourceHandle,
    Quantity quantity) const {
  std::lock_guard<std::mutex> cacheLock(m_cacheMutex);

  auto &worldCache = m_resourceCache[worldId];
  auto now = std::chrono::steady_clock::now();

  // Look for existing cache entry
  auto cacheIt = std::find_if(worldCache.begin(), worldCache.end(),
                              [resourceHandle](const ResourceCache &cache) {
                                return cache.resourceHandle == resourceHandle;
                              });

  if (cacheIt != worldCache.end()) {
    cacheIt->quantity = quantity;
    cacheIt->lastAccess = now;
    cacheIt->dirty = false;
    return;
  }

  // Add new cache entry
  if (worldCache.size() >= m_config.perWorldCacheSize) {
    // Remove oldest entry (LRU)
    auto oldest =
        std::min_element(worldCache.begin(), worldCache.end(),
                         [](const ResourceCache &a, const ResourceCache &b) {
                           return a.lastAccess < b.lastAccess;
                         });
    if (oldest != worldCache.end()) {
      worldCache.erase(oldest);
      if (m_config.enablePerformanceMonitoring) {
        m_stats.cacheEvictions++;
      }
    }
  }

  worldCache.push_back({resourceHandle, quantity, now, false});
}

WorldResourceManager::Quantity WorldResourceManager::getCachedResourceQuantity(
    const WorldId &worldId,
    const HammerEngine::ResourceHandle &resourceHandle) const {
  std::lock_guard<std::mutex> cacheLock(m_cacheMutex);

  auto worldIt = m_resourceCache.find(worldId);
  if (worldIt == m_resourceCache.end()) {
    if (m_config.enablePerformanceMonitoring) {
      m_stats.cacheMisses++;
    }
    return -1; // Cache miss
  }

  auto now = std::chrono::steady_clock::now();
  auto cacheIt = std::find_if(worldIt->second.begin(), worldIt->second.end(),
                              [resourceHandle](const ResourceCache &cache) {
                                return cache.resourceHandle == resourceHandle &&
                                       !cache.dirty;
                              });

  if (cacheIt != worldIt->second.end()) {
    cacheIt->lastAccess = now; // Update access time
    if (m_config.enablePerformanceMonitoring) {
      m_stats.cacheHits++;
    }
    return cacheIt->quantity;
  }

  if (m_config.enablePerformanceMonitoring) {
    m_stats.cacheMisses++;
  }
  return -1; // Cache miss
}

void WorldResourceManager::invalidateAggregateCache() const {
  std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
  m_aggregateCache.valid = false;

  // Mark all resource caches as dirty
  for (auto &[worldId, worldCache] : m_resourceCache) {
    for (auto &cache : worldCache) {
      cache.dirty = true;
    }
  }
}

void WorldResourceManager::updateAggregateCache() const {
  std::lock_guard<std::mutex> cacheLock(m_cacheMutex);

  auto now = std::chrono::steady_clock::now();
  if (m_aggregateCache.valid &&
      (now - m_aggregateCache.lastUpdate) < m_config.cacheExpiryTime) {
    return; // Cache still valid
  }

  // Rebuild aggregate cache
  m_aggregateCache.totals.clear();

  // Note: This requires the resource mutex to be held by caller
  for (const auto &[worldId, worldResources] : m_worldResources) {
    for (const auto &[resourceHandle, quantity] : worldResources) {
      m_aggregateCache.totals[resourceHandle] += quantity;
    }
  }

  m_aggregateCache.lastUpdate = now;
  m_aggregateCache.valid = true;

  if (m_config.enablePerformanceMonitoring) {
    m_stats.aggregateCacheRebuilds++;
  }
}

// Cache monitoring methods
size_t WorldResourceManager::getCacheSize(const WorldId &worldId) const {
  std::lock_guard<std::mutex> cacheLock(m_cacheMutex);

  auto it = m_resourceCache.find(worldId);
  return it != m_resourceCache.end() ? it->second.size() : 0;
}

size_t WorldResourceManager::getTotalCacheSize() const {
  std::lock_guard<std::mutex> cacheLock(m_cacheMutex);

  size_t total = 0;
  for (const auto &[worldId, cache] : m_resourceCache) {
    total += cache.size();
  }
  return total;
}

std::unordered_map<WorldResourceManager::WorldId, size_t>
WorldResourceManager::getAllCacheSizes() const {
  std::lock_guard<std::mutex> cacheLock(m_cacheMutex);

  std::unordered_map<WorldId, size_t> sizes;
  for (const auto &[worldId, cache] : m_resourceCache) {
    sizes[worldId] = cache.size();
  }
  return sizes;
}

void WorldResourceManager::logCacheStatus() const {
  auto stats = getStats();
  auto config = getConfig();

  WORLD_RESOURCE_INFO("=== WorldResourceManager Cache Status ===");
  WORLD_RESOURCE_INFO("Configuration:");
  WORLD_RESOURCE_INFO(std::format("  Per-world cache size: {}", config.perWorldCacheSize));
  WORLD_RESOURCE_INFO(std::format("  Cache expiry time: {}ms", config.cacheExpiryTime.count()));
  WORLD_RESOURCE_INFO(std::format("  Performance monitoring: {}",
      config.enablePerformanceMonitoring ? "enabled" : "disabled"));

  if (config.enablePerformanceMonitoring) {
    WORLD_RESOURCE_INFO("Performance Stats:");
    WORLD_RESOURCE_INFO(std::format("  Cache hit ratio: {}%",
                        stats.getCacheHitRatio() * 100.0));
    WORLD_RESOURCE_INFO(std::format("  Cache hits: {}",
                        stats.cacheHits.load()));
    WORLD_RESOURCE_INFO(std::format("  Cache misses: {}",
                        stats.cacheMisses.load()));
    WORLD_RESOURCE_INFO(std::format("  Cache evictions: {}",
                        stats.cacheEvictions.load()));
    WORLD_RESOURCE_INFO(std::format("  Aggregate cache rebuilds: {}",
                        stats.aggregateCacheRebuilds.load()));
  }

  WORLD_RESOURCE_INFO("Current Usage:");
  WORLD_RESOURCE_INFO(std::format("  Total cache entries: {}",
                      getTotalCacheSize()));
  WORLD_RESOURCE_INFO(std::format("  Worlds tracked: {}",
                      stats.worldsTracked.load()));
  WORLD_RESOURCE_INFO(std::format("  Memory usage: {} bytes", getMemoryUsage()));

  // Warn if performance is sub-optimal
  if (stats.worldsTracked.load() > config.maxWorldsBeforeWarning) {
    WORLD_RESOURCE_WARN(std::format("High world count ({}) may impact performance. "
                        "Consider optimizing world lifecycle management.",
                        stats.worldsTracked.load()));
  }

  if (config.enablePerformanceMonitoring && stats.getCacheHitRatio() < 0.7) {
    WORLD_RESOURCE_WARN(std::format("Low cache hit ratio ({}%). "
                        "Consider increasing per-world cache size.",
                        stats.getCacheHitRatio() * 100.0));
  }
}

bool WorldResourceManager::isPerformanceOptimal() const {
  auto stats = getStats();
  auto config = getConfig();

  // Check multiple performance indicators
  bool worldCountOk =
      stats.worldsTracked.load() <= config.maxWorldsBeforeWarning;
  bool cacheRatioOk =
      !config.enablePerformanceMonitoring || stats.getCacheHitRatio() >= 0.7;
  bool memoryUsageOk =
      getMemoryUsage() < (100 * 1024 * 1024); // Less than 100MB

  return worldCountOk && cacheRatioOk && memoryUsageOk;
}

void WorldResourceManager::registerEventHandlers() {
  try {
    EventManager &eventMgr = EventManager::Instance();

    // Register handler for world events (world loaded/unloaded)
    eventMgr.registerHandler(EventTypeId::World, [this](const EventData &data) {
      if (data.isActive() && data.event) {
        // Handle world-related events from WorldManager
        auto worldEvent = std::dynamic_pointer_cast<WorldEvent>(data.event);
        if (worldEvent) {
          handleWorldEvent(worldEvent);
        }
      }
    });

    WORLD_RESOURCE_DEBUG("WorldResourceManager event handlers registered");
  } catch (const std::exception &ex) {
    WORLD_RESOURCE_ERROR("Failed to register event handlers: " +
                         std::string(ex.what()));
  }
}

void WorldResourceManager::unregisterEventHandlers() {
  try {
    // EventManager handles cleanup automatically during shutdown
    WORLD_RESOURCE_DEBUG("WorldResourceManager event handlers unregistered");
  } catch (const std::exception &ex) {
    WORLD_RESOURCE_ERROR("Failed to unregister event handlers: " +
                         std::string(ex.what()));
  }
}

void WorldResourceManager::handleWorldEvent(
    std::shared_ptr<WorldEvent> worldEvent) {
  try {
    switch (worldEvent->getEventType()) {
    case WorldEventType::WorldLoaded: {
      auto loadedEvent =
          std::dynamic_pointer_cast<WorldLoadedEvent>(worldEvent);
      if (loadedEvent) {
        const std::string &worldId = loadedEvent->getWorldId();
        WORLD_RESOURCE_INFO(std::format("Received WorldLoadedEvent for: {}", worldId));

        // Check if world exists in resource tracking
        bool worldExists = false;
        {
          std::shared_lock<std::shared_mutex> lock(m_resourceMutex);
          worldExists =
              m_worldResources.find(worldId) != m_worldResources.end();
        }

        if (!worldExists) {
          // Only create resource tracking for worlds that actually exist
          // Verify the world actually exists in the WorldManager before
          // creating tracking NOTE: Removed auto-creation to prevent spurious
          // world creation from events
          WORLD_RESOURCE_WARN(std::format(
              "Received WorldLoadedEvent for non-existent world: {} - skipping resource tracking creation",
              worldId));
        } else {
          WORLD_RESOURCE_INFO(std::format("World already tracked: {}", worldId));
        }
      }
      break;
    }

    case WorldEventType::WorldUnloaded: {
      auto unloadedEvent =
          std::dynamic_pointer_cast<WorldUnloadedEvent>(worldEvent);
      if (unloadedEvent) {
        const std::string &worldId = unloadedEvent->getWorldId();
        WORLD_RESOURCE_INFO(std::format("Received WorldUnloadedEvent for: {}", worldId));

        // Optional: Remove world resources when unloaded
        // For now, keep the data in case world is reloaded
        // removeWorld(worldId);
      }
      break;
    }

    case WorldEventType::TileChanged: {
      auto tileEvent = std::dynamic_pointer_cast<TileChangedEvent>(worldEvent);
      if (tileEvent) {
        // Handle tile changes - could affect resource distributions
        WORLD_RESOURCE_DEBUG(std::format("Tile changed at ({}, {}) - {}",
                             tileEvent->getX(), tileEvent->getY(),
                             tileEvent->getChangeType()));
      }
      break;
    }

    default:
      // Handle other world event types as needed
      WORLD_RESOURCE_DEBUG(std::format("Received world event: {}", worldEvent->getName()));
      break;
    }
  } catch (const std::exception &ex) {
    WORLD_RESOURCE_ERROR("Error handling world event: " +
                         std::string(ex.what()));
  }
}

void WorldResourceManager::fireResourceChangeEvent(
    const WorldId &worldId, const HammerEngine::ResourceHandle &resourceHandle,
    Quantity oldQuantity, Quantity newQuantity, const std::string &reason) {
  try {
    // Only fire events for actual changes
    if (oldQuantity == newQuantity) {
      return;
    }

    // Trigger ResourceChange via EventManager hub (no registration needed)
    const EventManager &eventMgr = EventManager::Instance();
    eventMgr.triggerResourceChange(
        nullptr, // world-level (no specific owner)
        resourceHandle, static_cast<int>(oldQuantity),
        static_cast<int>(newQuantity), std::format("{}_world_", reason) + worldId,
        EventManager::DispatchMode::Deferred);

    WORLD_RESOURCE_DEBUG(std::format("ResourceChangeEvent fired for {} in world {}: {} -> {}",
                         resourceHandle.toString(), worldId,
                         oldQuantity, newQuantity));
  } catch (const std::exception &ex) {
    WORLD_RESOURCE_ERROR(std::format("Failed to fire ResourceChangeEvent: {}",
                                     ex.what()));
  }
}
