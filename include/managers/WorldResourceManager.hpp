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
 * @brief Configuration for WorldResourceManager caches
 */
struct WorldResourceManagerConfig {
  size_t perWorldCacheSize{16};                   // Max cache entries per world
  std::chrono::milliseconds cacheExpiryTime{100}; // Aggregate cache expiry time
  bool enablePerformanceMonitoring{true}; // Enable detailed performance stats
  size_t maxWorldsBeforeWarning{100};     // Warn when too many worlds tracked

  // Validation
  bool isValid() const {
    return perWorldCacheSize > 0 && perWorldCacheSize <= 1000 &&
           cacheExpiryTime.count() > 0 && cacheExpiryTime.count() <= 10000 &&
           maxWorldsBeforeWarning > 0;
  }
};

/**
 * @brief Enhanced statistics with cache performance monitoring
 */
struct WorldResourceStats {
  std::atomic<uint64_t> totalResourcesTracked{0};
  std::atomic<uint64_t> totalTransactions{0};
  std::atomic<uint64_t> addOperations{0};
  std::atomic<uint64_t> removeOperations{0};
  std::atomic<uint64_t> worldsTracked{0};

  // Cache performance stats
  std::atomic<uint64_t> cacheHits{0};
  std::atomic<uint64_t> cacheMisses{0};
  std::atomic<uint64_t> cacheEvictions{0};
  std::atomic<uint64_t> aggregateCacheRebuilds{0};

  // Custom copy constructor
  WorldResourceStats() = default;
  WorldResourceStats(const WorldResourceStats &other)
      : totalResourcesTracked(other.totalResourcesTracked.load()),
        totalTransactions(other.totalTransactions.load()),
        addOperations(other.addOperations.load()),
        removeOperations(other.removeOperations.load()),
        worldsTracked(other.worldsTracked.load()),
        cacheHits(other.cacheHits.load()),
        cacheMisses(other.cacheMisses.load()),
        cacheEvictions(other.cacheEvictions.load()),
        aggregateCacheRebuilds(other.aggregateCacheRebuilds.load()) {}

  // Custom assignment operator
  WorldResourceStats &operator=(const WorldResourceStats &other) {
    if (this != &other) {
      totalResourcesTracked = other.totalResourcesTracked.load();
      totalTransactions = other.totalTransactions.load();
      addOperations = other.addOperations.load();
      removeOperations = other.removeOperations.load();
      worldsTracked = other.worldsTracked.load();
      cacheHits = other.cacheHits.load();
      cacheMisses = other.cacheMisses.load();
      cacheEvictions = other.cacheEvictions.load();
      aggregateCacheRebuilds = other.aggregateCacheRebuilds.load();
    }
    return *this;
  }

  void reset() {
    totalResourcesTracked = 0;
    totalTransactions = 0;
    addOperations = 0;
    removeOperations = 0;
    worldsTracked = 0;
    cacheHits = 0;
    cacheMisses = 0;
    cacheEvictions = 0;
    aggregateCacheRebuilds = 0;
  }

  // Calculate cache hit ratio (0.0 to 1.0)
  double getCacheHitRatio() const {
    uint64_t hits = cacheHits.load();
    uint64_t misses = cacheMisses.load();
    uint64_t total = hits + misses;
    return total > 0 ? static_cast<double>(hits) / total : 0.0;
  }
};

/**
 * @brief Transaction result for resource operations
 */
enum class ResourceTransactionResult {
  Success,
  InsufficientResources,
  InvalidResourceHandle,
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
  ResourceTransactionResult
  addResource(const WorldId &worldId,
              const HammerEngine::ResourceHandle &resourceHandle,
              Quantity quantity);
  ResourceTransactionResult
  removeResource(const WorldId &worldId,
                 const HammerEngine::ResourceHandle &resourceHandle,
                 Quantity quantity);
  ResourceTransactionResult
  setResource(const WorldId &worldId,
              const HammerEngine::ResourceHandle &resourceHandle,
              Quantity quantity);

  // Resource quantity queries
  Quantity
  getResourceQuantity(const WorldId &worldId,
                      const HammerEngine::ResourceHandle &resourceHandle) const;
  bool hasResource(const WorldId &worldId,
                   const HammerEngine::ResourceHandle &resourceHandle,
                   Quantity minimumQuantity = 1) const;

  // Aggregation queries
  Quantity getTotalResourceQuantity(
      const HammerEngine::ResourceHandle &resourceHandle) const;
  std::unordered_map<HammerEngine::ResourceHandle, Quantity>
  getWorldResources(const WorldId &worldId) const;
  std::unordered_map<HammerEngine::ResourceHandle, Quantity>
  getAllResourceTotals() const;

  // Batch operations
  bool transferResource(const WorldId &fromWorldId, const WorldId &toWorldId,
                        const HammerEngine::ResourceHandle &resourceHandle,
                        Quantity quantity);
  bool transferAllResources(const WorldId &fromWorldId,
                            const WorldId &toWorldId);

  // Configuration
  bool configure(const WorldResourceManagerConfig &config);
  WorldResourceManagerConfig getConfig() const;

  // Statistics and monitoring
  WorldResourceStats getStats() const;
  void resetStats() { m_stats.reset(); }
  size_t getMemoryUsage() const;

  // Cache monitoring
  size_t getCacheSize(const WorldId &worldId) const;
  size_t getTotalCacheSize() const;
  std::unordered_map<WorldId, size_t> getAllCacheSizes() const;
  double getCacheHitRatio() const { return m_stats.getCacheHitRatio(); }

  // Performance diagnostics
  void logCacheStatus() const;
  bool isPerformanceOptimal() const;

  // Validation
  bool isValidWorldId(const WorldId &worldId) const;
  bool isValidResourceHandle(
      const HammerEngine::ResourceHandle &resourceHandle) const;
  bool isValidQuantity(Quantity quantity) const;

private:
  WorldResourceManager() = default;
  ~WorldResourceManager();

  // Prevent copying
  WorldResourceManager(const WorldResourceManager &) = delete;
  WorldResourceManager &operator=(const WorldResourceManager &) = delete;

  // Internal data structures
  // Map of WorldId -> Map of ResourceHandle -> Quantity
  std::unordered_map<WorldId,
                     std::unordered_map<HammerEngine::ResourceHandle, Quantity>>
      m_worldResources;

  // Optimization: Resource access cache for frequently accessed resources
  struct ResourceCache {
    HammerEngine::ResourceHandle resourceHandle;
    Quantity quantity;
    std::chrono::steady_clock::time_point lastAccess;
    bool dirty{false};
  };

  // Cache for the most recently accessed resources per world (LRU-style)
  mutable std::unordered_map<WorldId, std::vector<ResourceCache>>
      m_resourceCache;

  // Spatial partitioning for resource aggregation queries
  struct ResourceAggregateCache {
    std::unordered_map<HammerEngine::ResourceHandle, Quantity> totals;
    std::chrono::steady_clock::time_point lastUpdate;
    bool valid{false};
  };
  mutable ResourceAggregateCache m_aggregateCache;

  // Configuration
  WorldResourceManagerConfig m_config;

  mutable WorldResourceStats m_stats;
  std::atomic<bool> m_initialized{false};
  bool m_isShutdown{false};

  // Thread safety
  mutable std::shared_mutex m_resourceMutex;
  mutable std::mutex m_cacheMutex;

  // Helper methods
  void updateStats(bool isAdd, Quantity quantity);
  bool validateParameters(const WorldId &worldId,
                          const HammerEngine::ResourceHandle &resourceHandle,
                          Quantity quantity) const;
  void ensureWorldExists(const WorldId &worldId);

  // Cache management methods
  void updateResourceCache(const WorldId &worldId,
                           const HammerEngine::ResourceHandle &resourceHandle,
                           Quantity quantity) const;
  Quantity getCachedResourceQuantity(
      const WorldId &worldId,
      const HammerEngine::ResourceHandle &resourceHandle) const;
  void invalidateAggregateCache() const;
  void updateAggregateCache() const;
};

#endif // WORLD_RESOURCE_MANAGER_HPP