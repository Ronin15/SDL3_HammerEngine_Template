/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef RESOURCE_TEMPLATE_MANAGER_HPP
#define RESOURCE_TEMPLATE_MANAGER_HPP

#include "entities/Resource.hpp"
#include "utils/ResourceHandle.hpp"
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
class EventManager;

/**
 * @brief Resource creation statistics
 */
struct ResourceStats {
  std::atomic<uint64_t> templatesLoaded{0};
  std::atomic<uint64_t> resourcesCreated{0};
  std::atomic<uint64_t> resourcesDestroyed{0};

  // Custom copy constructor
  ResourceStats() = default;
  ResourceStats(const ResourceStats &other)
      : templatesLoaded(other.templatesLoaded.load()),
        resourcesCreated(other.resourcesCreated.load()),
        resourcesDestroyed(other.resourcesDestroyed.load()) {}

  // Custom assignment operator
  ResourceStats &operator=(const ResourceStats &other) {
    if (this != &other) {
      templatesLoaded = other.templatesLoaded.load();
      resourcesCreated = other.resourcesCreated.load();
      resourcesDestroyed = other.resourcesDestroyed.load();
    }
    return *this;
  }

  void reset() {
    templatesLoaded = 0;
    resourcesCreated = 0;
    resourcesDestroyed = 0;
  }
};

/**
 * @brief Singleton ResourceTemplateManager for managing resource templates
 */
class ResourceTemplateManager {
public:
  static ResourceTemplateManager &Instance();

  // Core functionality
  bool init();
  bool isInitialized() const { return m_initialized.load(); }
  void clean();

  // Resource template management
  bool registerResourceTemplate(const ResourcePtr &resource);
  bool removeResourceTemplate(
      HammerEngine::ResourceHandle handle); // Remove and release handle
  ResourcePtr getResourceTemplate(HammerEngine::ResourceHandle handle) const;
  ResourcePtr
  getResourceByName(const std::string &name) const; // O(1) display name lookup
  ResourcePtr
  getResourceById(const std::string &id) const; // O(1) JSON ID lookup
  HammerEngine::ResourceHandle
  getHandleByName(const std::string &name) const; // O(1) name to handle lookup
  HammerEngine::ResourceHandle
  getHandleById(const std::string &id) const; // O(1) ID to handle lookup
  std::vector<ResourcePtr>
  getResourcesByCategory(ResourceCategory category) const;
  std::vector<ResourcePtr> getResourcesByType(ResourceType type) const;

  // Fast property access (cache-optimized, no shared_ptr dereferencing)
  int getMaxStackSize(HammerEngine::ResourceHandle handle) const;
  float getValue(HammerEngine::ResourceHandle handle) const;
  ResourceCategory getCategory(HammerEngine::ResourceHandle handle) const;
  ResourceType getType(HammerEngine::ResourceHandle handle) const;

  // Cache-friendly bulk operations for better performance
  std::vector<int> getMaxStackSizes(
      const std::vector<HammerEngine::ResourceHandle> &handles) const;
  std::vector<float>
  getValues(const std::vector<HammerEngine::ResourceHandle> &handles) const;
  void
  getPropertiesBatch(const std::vector<HammerEngine::ResourceHandle> &handles,
                     std::vector<int> &maxStackSizes,
                     std::vector<float> &values,
                     std::vector<ResourceCategory> &categories,
                     std::vector<ResourceType> &types) const;

  // Handle management
  HammerEngine::ResourceHandle generateHandle();
  bool isValidHandle(HammerEngine::ResourceHandle handle) const;
  void releaseHandle(
      HammerEngine::ResourceHandle handle); // Mark handle as freed for reuse

  // Statistics
  ResourceStats getStats() const;
  void resetStats() { m_stats.reset(); }

  // Resource creation
  ResourcePtr createResource(HammerEngine::ResourceHandle handle) const;

  // JSON loading methods
  bool loadResourcesFromJson(const std::string &filename);
  bool loadResourcesFromJsonString(const std::string &jsonString);

  // Query methods
  size_t getResourceTemplateCount() const;
  bool hasResourceTemplate(HammerEngine::ResourceHandle handle) const;
  size_t getMemoryUsage() const;

private:
  ResourceTemplateManager() = default;
  ~ResourceTemplateManager();

  // Prevent copying
  ResourceTemplateManager(const ResourceTemplateManager &) = delete;
  ResourceTemplateManager &operator=(const ResourceTemplateManager &) = delete;

  // Internal data
  std::unordered_map<HammerEngine::ResourceHandle, ResourcePtr>
      m_resourceTemplates;

  // SoA optimization for frequently accessed properties (cache-friendly)
  std::unordered_map<HammerEngine::ResourceHandle, int> m_maxStackSizes;
  std::unordered_map<HammerEngine::ResourceHandle, float> m_values;
  std::unordered_map<HammerEngine::ResourceHandle, ResourceCategory>
      m_categories;
  std::unordered_map<HammerEngine::ResourceHandle, ResourceType> m_types;

  // Category and type indexes for fast filtering
  std::unordered_map<ResourceCategory,
                     std::vector<HammerEngine::ResourceHandle>>
      m_categoryIndex;
  std::unordered_map<ResourceType, std::vector<HammerEngine::ResourceHandle>>
      m_typeIndex;

  // Name index for O(1) name-based lookups
  std::unordered_map<std::string, HammerEngine::ResourceHandle> m_nameIndex;

  // ID index for O(1) JSON ID-based lookups (primary identifier)
  std::unordered_map<std::string, HammerEngine::ResourceHandle> m_idIndex;

  // Handle generation with proper generation tracking
  std::atomic<HammerEngine::ResourceHandle::HandleId> m_nextHandleId{
      1}; // Start from 1, 0 is invalid

  // Generation tracking for reused handles - prevents stale handle bugs
  std::unordered_map<HammerEngine::ResourceHandle::HandleId,
                     HammerEngine::ResourceHandle::Generation>
      m_handleGenerations;
  std::vector<HammerEngine::ResourceHandle::HandleId>
      m_freedHandleIds; // Pool of freed IDs for reuse
  mutable std::mutex
      m_handleMutex; // Protects generation and freed ID management

  mutable ResourceStats m_stats;
  std::atomic<bool> m_initialized{false};
  bool m_isShutdown{false};

  // Thread safety
  mutable std::shared_mutex m_resourceMutex;
  mutable std::mutex m_indexMutex;

  // Helper methods
  void updateIndexes(HammerEngine::ResourceHandle handle,
                     ResourceCategory category, ResourceType type);
  void updateNameIndex(HammerEngine::ResourceHandle handle,
                       const std::string &name);
  void updateIdIndex(HammerEngine::ResourceHandle handle,
                     const std::string &id);
  void removeFromIndexes(HammerEngine::ResourceHandle handle);
  bool checkForDuplicateName(const std::string &name,
                             HammerEngine::ResourceHandle currentHandle) const;

  // Internal registration method (no locking - assumes lock is already held)
  bool registerResourceTemplateInternal(const ResourcePtr &resource);

  // Default resource creation
  void createDefaultResources();
};

#endif // RESOURCE_TEMPLATE_MANAGER_HPP