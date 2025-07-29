/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef WORLD_RESOURCE_MANAGER_HPP
#define WORLD_RESOURCE_MANAGER_HPP

#include "utils/ResourceHandle.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
class EventManager;

/**
 * @brief World resource quantity statistics
 */
struct WorldResourceStats {
  std::atomic<uint64_t> totalResourcesTracked{0};
  std::atomic<uint64_t> totalTransactions{0};
  std::atomic<uint64_t> addOperations{0};
  std::atomic<uint64_t> removeOperations{0};
  std::atomic<uint64_t> worldsTracked{0};

  // Custom copy constructor
  WorldResourceStats() = default;
  WorldResourceStats(const WorldResourceStats &other)
      : totalResourcesTracked(other.totalResourcesTracked.load()),
        totalTransactions(other.totalTransactions.load()),
        addOperations(other.addOperations.load()),
        removeOperations(other.removeOperations.load()),
        worldsTracked(other.worldsTracked.load()) {}

  // Custom assignment operator
  WorldResourceStats &operator=(const WorldResourceStats &other) {
    if (this != &other) {
      totalResourcesTracked = other.totalResourcesTracked.load();
      totalTransactions = other.totalTransactions.load();
      addOperations = other.addOperations.load();
      removeOperations = other.removeOperations.load();
      worldsTracked = other.worldsTracked.load();
    }
    return *this;
  }

  void reset() {
    totalResourcesTracked = 0;
    totalTransactions = 0;
    addOperations = 0;
    removeOperations = 0;
    worldsTracked = 0;
  }
};

/**
 * @brief Transaction result for resource operations
 */
enum class ResourceTransactionResult {
  Success,
  InsufficientResources,
  InvalidResourceId,
  InvalidWorldId,
  InvalidQuantity,
  SystemError
};

/**
 * @brief Singleton WorldResourceManager for tracking global resource quantities
 *
 * This manager tracks the total quantities of each resource across all entities
 * in a world. It supports multiple worlds and provides thread-safe operations
 * for adding, removing, and querying resource quantities.
 */
class WorldResourceManager {
public:
  // World ID type - using string for flexibility (could be UUID, integer, etc.)
  using WorldId = std::string;
  using ResourceId = HammerEngine::ResourceHandle;
  using Quantity = int64_t;

  static WorldResourceManager &Instance();

  // Core functionality
  bool init();
  bool isInitialized() const { return m_initialized.load(); }
  void clean();

  // World management
  bool createWorld(const WorldId &worldId);
  bool removeWorld(const WorldId &worldId);
  bool hasWorld(const WorldId &worldId) const;
  std::vector<WorldId> getWorldIds() const;

  // Resource quantity tracking
  ResourceTransactionResult addResource(const WorldId &worldId,
                                        const ResourceId &resourceId,
                                        Quantity quantity);
  ResourceTransactionResult removeResource(const WorldId &worldId,
                                           const ResourceId &resourceId,
                                           Quantity quantity);
  ResourceTransactionResult setResource(const WorldId &worldId,
                                        const ResourceId &resourceId,
                                        Quantity quantity);

  // Resource quantity queries
  Quantity getResourceQuantity(const WorldId &worldId,
                               const ResourceId &resourceId) const;
  bool hasResource(const WorldId &worldId, const ResourceId &resourceId,
                   Quantity minimumQuantity = 1) const;

  // Aggregation queries
  Quantity getTotalResourceQuantity(const ResourceId &resourceId) const;
  std::unordered_map<ResourceId, Quantity>
  getWorldResources(const WorldId &worldId) const;
  std::unordered_map<ResourceId, Quantity> getAllResourceTotals() const;

  // Batch operations
  bool transferResource(const WorldId &fromWorldId, const WorldId &toWorldId,
                        const ResourceId &resourceId, Quantity quantity);
  bool transferAllResources(const WorldId &fromWorldId,
                            const WorldId &toWorldId);

  // Statistics and monitoring
  WorldResourceStats getStats() const;
  void resetStats() { m_stats.reset(); }
  size_t getMemoryUsage() const;

  // Validation
  bool isValidWorldId(const WorldId &worldId) const;
  bool isValidResourceId(const ResourceId &resourceId) const;
  bool isValidQuantity(Quantity quantity) const;

private:
  WorldResourceManager() = default;
  ~WorldResourceManager();

  // Prevent copying
  WorldResourceManager(const WorldResourceManager &) = delete;
  WorldResourceManager &operator=(const WorldResourceManager &) = delete;

  // Internal data structures
  // Map of WorldId -> Map of ResourceId -> Quantity
  std::unordered_map<WorldId, std::unordered_map<ResourceId, Quantity>>
      m_worldResources;

  // Optimization: Resource access cache for frequently accessed resources
  struct ResourceCache {
    ResourceId resourceId;
    Quantity quantity;
    std::chrono::steady_clock::time_point lastAccess;
    bool dirty{false};
  };

  // Cache for the most recently accessed resources per world (LRU-style)
  mutable std::unordered_map<WorldId, std::vector<ResourceCache>>
      m_resourceCache;
  static constexpr size_t MAX_CACHE_SIZE = 16; // Per world cache limit

  // Spatial partitioning for resource aggregation queries
  struct ResourceAggregateCache {
    std::unordered_map<ResourceId, Quantity> totals;
    std::chrono::steady_clock::time_point lastUpdate;
    bool valid{false};
  };
  mutable ResourceAggregateCache m_aggregateCache;
  static constexpr std::chrono::milliseconds CACHE_EXPIRY_TIME{
      100}; // 100ms cache validity

  mutable WorldResourceStats m_stats;
  std::atomic<bool> m_initialized{false};
  bool m_isShutdown{false};

  // Thread safety
  mutable std::shared_mutex m_resourceMutex;
  mutable std::mutex m_cacheMutex;

  // Helper methods
  void updateStats(bool isAdd, Quantity quantity);
  bool validateParameters(const WorldId &worldId, const ResourceId &resourceId,
                          Quantity quantity) const;
  void ensureWorldExists(const WorldId &worldId);

  // Cache management methods
  void updateResourceCache(const WorldId &worldId, const ResourceId &resourceId,
                           Quantity quantity) const;
  Quantity getCachedResourceQuantity(const WorldId &worldId,
                                     const ResourceId &resourceId) const;
  void invalidateAggregateCache() const;
  void updateAggregateCache() const;
};

#endif // WORLD_RESOURCE_MANAGER_HPP