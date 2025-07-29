/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/WorldResourceManager.hpp"
#include "core/Logger.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include <algorithm>
#include <cassert>

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
    m_worldResources["default"] = std::unordered_map<ResourceId, Quantity>();

    m_initialized.store(true, std::memory_order_release);
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

bool WorldResourceManager::createWorld(const WorldId &worldId) {
  if (!isValidWorldId(worldId)) {
    WORLD_RESOURCE_ERROR(
        "WorldResourceManager::createWorld - Invalid world ID: " + worldId);
    return false;
  }

  std::lock_guard<std::shared_mutex> lock(m_resourceMutex);

  if (m_worldResources.find(worldId) != m_worldResources.end()) {
    WORLD_RESOURCE_WARN(
        "WorldResourceManager::createWorld - World already exists: " + worldId);
    return false;
  }

  try {
    m_worldResources[worldId] = std::unordered_map<ResourceId, Quantity>();
    m_stats.worldsTracked.fetch_add(1, std::memory_order_relaxed);

    WORLD_RESOURCE_INFO("Created world: " + worldId);
    return true;
  } catch (const std::exception &ex) {
    WORLD_RESOURCE_ERROR("WorldResourceManager::createWorld - Exception: " +
                         std::string(ex.what()));
    return false;
  }
}

bool WorldResourceManager::removeWorld(const WorldId &worldId) {
  if (!isValidWorldId(worldId)) {
    WORLD_RESOURCE_ERROR(
        "WorldResourceManager::removeWorld - Invalid world ID: " + worldId);
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
        "WorldResourceManager::removeWorld - World not found: " + worldId);
    return false;
  }

  try {
    m_worldResources.erase(it);
    m_stats.worldsTracked.fetch_sub(1, std::memory_order_relaxed);

    WORLD_RESOURCE_INFO("Removed world: " + worldId);
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
    const WorldId &worldId, const ResourceId &resourceId, Quantity quantity) {
  // Validate parameters individually to return appropriate error codes
  if (!isValidWorldId(worldId)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid world ID: " + worldId);
    return ResourceTransactionResult::InvalidWorldId;
  }

  if (!isValidResourceId(resourceId)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid resource ID: " +
                         resourceId.toString());
    return ResourceTransactionResult::InvalidResourceId;
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

  try {
    ensureWorldExists(worldId);

    auto &worldResources = m_worldResources[worldId];

    // Check for overflow (only if quantity > 0)
    if (quantity > 0) {
      Quantity currentQuantity = worldResources[resourceId];
      if (currentQuantity > std::numeric_limits<Quantity>::max() - quantity) {
        WORLD_RESOURCE_ERROR(
            "WorldResourceManager::addResource - Quantity overflow");
        return ResourceTransactionResult::SystemError;
      }
      worldResources[resourceId] = currentQuantity + quantity;

      // Invalidate caches when resources change
      invalidateAggregateCache();
      updateResourceCache(worldId, resourceId, currentQuantity + quantity);
    }
    // If quantity is 0, we do nothing but still consider it successful

    updateStats(true, quantity);

    WORLD_RESOURCE_DEBUG("Added " + std::to_string(quantity) + " " +
                         resourceId.toString() + " to world " + worldId);
    return ResourceTransactionResult::Success;
  } catch (const std::exception &ex) {
    WORLD_RESOURCE_ERROR("WorldResourceManager::addResource - Exception: " +
                         std::string(ex.what()));
    return ResourceTransactionResult::SystemError;
  }
}

ResourceTransactionResult WorldResourceManager::removeResource(
    const WorldId &worldId, const ResourceId &resourceId, Quantity quantity) {
  // Validate parameters individually to return appropriate error codes
  if (!isValidWorldId(worldId)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid world ID: " + worldId);
    return ResourceTransactionResult::InvalidWorldId;
  }

  if (!isValidResourceId(resourceId)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid resource ID: " +
                         resourceId.toString());
    return ResourceTransactionResult::InvalidResourceId;
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

  try {
    ensureWorldExists(worldId);

    auto &worldResources = m_worldResources[worldId];
    Quantity currentQuantity = worldResources[resourceId];

    // If quantity is 0, we do nothing but still consider it successful
    if (quantity == 0) {
      WORLD_RESOURCE_DEBUG("Removed " + std::to_string(quantity) + " " +
                           resourceId.toString() + " from world " + worldId);
      return ResourceTransactionResult::Success;
    }

    if (currentQuantity < quantity) {
      WORLD_RESOURCE_WARN(std::string("WorldResourceManager::removeResource - "
                                      "Insufficient resources. ") +
                          "Current: " + std::to_string(currentQuantity) +
                          ", Requested: " + std::to_string(quantity));
      return ResourceTransactionResult::InsufficientResources;
    }

    worldResources[resourceId] = currentQuantity - quantity;
    updateStats(false, quantity);

    // Invalidate caches when resources change
    invalidateAggregateCache();
    updateResourceCache(worldId, resourceId, currentQuantity - quantity);

    WORLD_RESOURCE_DEBUG("Removed " + std::to_string(quantity) + " " +
                         resourceId.toString() + " from world " + worldId);
    return ResourceTransactionResult::Success;
  } catch (const std::exception &ex) {
    WORLD_RESOURCE_ERROR("WorldResourceManager::removeResource - Exception: " +
                         std::string(ex.what()));
    return ResourceTransactionResult::SystemError;
  }
}

ResourceTransactionResult WorldResourceManager::setResource(
    const WorldId &worldId, const ResourceId &resourceId, Quantity quantity) {
  // Validate parameters individually to return appropriate error codes
  if (!isValidWorldId(worldId)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid world ID: " + worldId);
    return ResourceTransactionResult::InvalidWorldId;
  }

  if (!isValidResourceId(resourceId)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid resource ID: " +
                         resourceId.toString());
    return ResourceTransactionResult::InvalidResourceId;
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

  try {
    ensureWorldExists(worldId);

    auto &worldResources = m_worldResources[worldId];
    Quantity oldQuantity = worldResources[resourceId];
    worldResources[resourceId] = quantity;

    // Update stats based on the net change
    Quantity netChange = quantity - oldQuantity;
    if (netChange != 0) {
      updateStats(netChange > 0, std::abs(netChange));

      // Invalidate caches when resources change
      invalidateAggregateCache();
      updateResourceCache(worldId, resourceId, quantity);
    }

    WORLD_RESOURCE_DEBUG("Set " + resourceId.toString() + " to " +
                         std::to_string(quantity) + " in world " + worldId);
    return ResourceTransactionResult::Success;
  } catch (const std::exception &ex) {
    WORLD_RESOURCE_ERROR("WorldResourceManager::setResource - Exception: " +
                         std::string(ex.what()));
    return ResourceTransactionResult::SystemError;
  }
}

WorldResourceManager::Quantity
WorldResourceManager::getResourceQuantity(const WorldId &worldId,
                                          const ResourceId &resourceId) const {
  // Try cache first for performance
  Quantity cachedQuantity = getCachedResourceQuantity(worldId, resourceId);
  if (cachedQuantity >= 0) {
    return cachedQuantity;
  }

  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);

  auto worldIt = m_worldResources.find(worldId);
  if (worldIt == m_worldResources.end()) {
    updateResourceCache(worldId, resourceId, 0);
    return 0;
  }

  auto resourceIt = worldIt->second.find(resourceId);
  Quantity quantity =
      (resourceIt != worldIt->second.end()) ? resourceIt->second : 0;

  // Update cache for future access
  updateResourceCache(worldId, resourceId, quantity);

  return quantity;
}

bool WorldResourceManager::hasResource(const WorldId &worldId,
                                       const ResourceId &resourceId,
                                       Quantity minimumQuantity) const {
  return getResourceQuantity(worldId, resourceId) >= minimumQuantity;
}

WorldResourceManager::Quantity WorldResourceManager::getTotalResourceQuantity(
    const ResourceId &resourceId) const {
  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);

  Quantity total = 0;
  for (const auto &[worldId, worldResources] : m_worldResources) {
    auto resourceIt = worldResources.find(resourceId);
    if (resourceIt != worldResources.end()) {
      total += resourceIt->second;
    }
  }

  return total;
}

std::unordered_map<WorldResourceManager::ResourceId,
                   WorldResourceManager::Quantity>
WorldResourceManager::getWorldResources(const WorldId &worldId) const {
  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);

  auto worldIt = m_worldResources.find(worldId);
  if (worldIt != m_worldResources.end()) {
    return worldIt->second; // Copy the map
  }

  return {}; // Return empty map if world not found
}

std::unordered_map<WorldResourceManager::ResourceId,
                   WorldResourceManager::Quantity>
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
  std::unordered_map<ResourceId, Quantity> totals;

  for (const auto &[worldId, worldResources] : m_worldResources) {
    for (const auto &[resourceId, quantity] : worldResources) {
      totals[resourceId] += quantity;
    }
  }

  return totals;
}

bool WorldResourceManager::transferResource(const WorldId &fromWorldId,
                                            const WorldId &toWorldId,
                                            const ResourceId &resourceId,
                                            Quantity quantity) {
  if (!validateParameters(fromWorldId, resourceId, quantity) ||
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

  try {
    ensureWorldExists(fromWorldId);
    ensureWorldExists(toWorldId);

    auto &fromResources = m_worldResources[fromWorldId];
    auto &toResources = m_worldResources[toWorldId];

    Quantity fromQuantity = fromResources[resourceId];
    if (fromQuantity < quantity) {
      WORLD_RESOURCE_WARN(
          "WorldResourceManager::transferResource - Insufficient resources");
      return false;
    }

    // Check for overflow in destination
    Quantity toQuantity = toResources[resourceId];
    if (toQuantity > std::numeric_limits<Quantity>::max() - quantity) {
      WORLD_RESOURCE_ERROR(
          "WorldResourceManager::transferResource - Quantity overflow");
      return false;
    }

    fromResources[resourceId] = fromQuantity - quantity;
    toResources[resourceId] = toQuantity + quantity;

    m_stats.totalTransactions.fetch_add(1, std::memory_order_relaxed);

    // Invalidate caches when resources change
    invalidateAggregateCache();
    updateResourceCache(fromWorldId, resourceId, fromQuantity - quantity);
    updateResourceCache(toWorldId, resourceId, toQuantity + quantity);

    WORLD_RESOURCE_DEBUG("Transferred " + std::to_string(quantity) + " " +
                         resourceId.toString() + " from " + fromWorldId +
                         " to " + toWorldId);
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

  try {
    ensureWorldExists(fromWorldId);
    ensureWorldExists(toWorldId);

    auto &fromResources = m_worldResources[fromWorldId];
    auto &toResources = m_worldResources[toWorldId];

    for (const auto &[resourceId, quantity] : fromResources) {
      if (quantity > 0) {
        toResources[resourceId] += quantity;
      }
    }

    fromResources.clear();
    m_stats.totalTransactions.fetch_add(1, std::memory_order_relaxed);

    // Invalidate all caches when doing bulk transfer
    invalidateAggregateCache();

    WORLD_RESOURCE_INFO("Transferred all resources from " + fromWorldId +
                        " to " + toWorldId);
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
    totalSize +=
        sizeof(std::unordered_map<ResourceId, Quantity>); // World resources map

    for (const auto &[resourceId, quantity] : worldResources) {
      totalSize += sizeof(resourceId); // Size of actual resource handle
      totalSize += sizeof(quantity);   // Size of actual quantity value
      // Each resource entry also has overhead from hash table bucket
      totalSize += sizeof(std::pair<ResourceId, Quantity>);
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

bool WorldResourceManager::isValidResourceId(
    const ResourceId &resourceId) const {
  return resourceId.isValid();
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

bool WorldResourceManager::validateParameters(const WorldId &worldId,
                                              const ResourceId &resourceId,
                                              Quantity quantity) const {
  if (!isValidWorldId(worldId)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid world ID: " + worldId);
    return false;
  }

  if (!isValidResourceId(resourceId)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid resource ID: " +
                         resourceId.toString());
    return false;
  }

  if (!isValidQuantity(quantity)) {
    WORLD_RESOURCE_ERROR("WorldResourceManager - Invalid quantity: " +
                         std::to_string(quantity));
    return false;
  }

  return true;
}

void WorldResourceManager::ensureWorldExists(const WorldId &worldId) {
  // Assumes we already have a write lock
  auto [it, inserted] = m_worldResources.try_emplace(
      worldId, std::unordered_map<ResourceId, Quantity>());
  if (inserted) {
    m_stats.worldsTracked.fetch_add(1, std::memory_order_relaxed);
    WORLD_RESOURCE_DEBUG("Auto-created world: " + worldId);
  }
}

// Cache management methods
void WorldResourceManager::updateResourceCache(const WorldId &worldId,
                                               const ResourceId &resourceId,
                                               Quantity quantity) const {
  std::lock_guard<std::mutex> cacheLock(m_cacheMutex);

  auto &worldCache = m_resourceCache[worldId];
  auto now = std::chrono::steady_clock::now();

  // Look for existing cache entry
  for (auto &cache : worldCache) {
    if (cache.resourceId == resourceId) {
      cache.quantity = quantity;
      cache.lastAccess = now;
      cache.dirty = false;
      return;
    }
  }

  // Add new cache entry
  if (worldCache.size() >= MAX_CACHE_SIZE) {
    // Remove oldest entry (LRU)
    auto oldest =
        std::min_element(worldCache.begin(), worldCache.end(),
                         [](const ResourceCache &a, const ResourceCache &b) {
                           return a.lastAccess < b.lastAccess;
                         });
    if (oldest != worldCache.end()) {
      worldCache.erase(oldest);
    }
  }

  worldCache.push_back({resourceId, quantity, now, false});
}

WorldResourceManager::Quantity WorldResourceManager::getCachedResourceQuantity(
    const WorldId &worldId, const ResourceId &resourceId) const {
  std::lock_guard<std::mutex> cacheLock(m_cacheMutex);

  auto worldIt = m_resourceCache.find(worldId);
  if (worldIt == m_resourceCache.end()) {
    return -1; // Cache miss
  }

  auto now = std::chrono::steady_clock::now();
  for (auto &cache : worldIt->second) {
    if (cache.resourceId == resourceId && !cache.dirty) {
      cache.lastAccess = now; // Update access time
      return cache.quantity;
    }
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
      (now - m_aggregateCache.lastUpdate) < CACHE_EXPIRY_TIME) {
    return; // Cache still valid
  }

  // Rebuild aggregate cache
  m_aggregateCache.totals.clear();

  // Note: This requires the resource mutex to be held by caller
  for (const auto &[worldId, worldResources] : m_worldResources) {
    for (const auto &[resourceId, quantity] : worldResources) {
      m_aggregateCache.totals[resourceId] += quantity;
    }
  }

  m_aggregateCache.lastUpdate = now;
  m_aggregateCache.valid = true;
}
