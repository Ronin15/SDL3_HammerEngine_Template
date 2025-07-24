/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef RESOURCE_TEMPLATE_MANAGER_HPP
#define RESOURCE_TEMPLATE_MANAGER_HPP

#include "entities/Resource.hpp"
#include <atomic>
#include <memory>
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
  bool isInitialized() const { return m_initialized; }
  void clean();

  // Resource template management
  bool registerResourceTemplate(const ResourcePtr &resource);
  ResourcePtr getResourceTemplate(const std::string &resourceId) const;
  std::vector<ResourcePtr>
  getResourcesByCategory(ResourceCategory category) const;
  std::vector<ResourcePtr> getResourcesByType(ResourceType type) const;

  // Statistics
  ResourceStats getStats() const;
  void resetStats() { m_stats.reset(); }

  // Resource creation
  ResourcePtr createResource(const std::string &resourceId) const;

  // Query methods
  size_t getResourceTemplateCount() const;
  bool hasResourceTemplate(const std::string &resourceId) const;
  size_t getMemoryUsage() const;

private:
  ResourceTemplateManager() = default;
  ~ResourceTemplateManager();

  // Prevent copying
  ResourceTemplateManager(const ResourceTemplateManager &) = delete;
  ResourceTemplateManager &operator=(const ResourceTemplateManager &) = delete;

  // Internal data
  std::unordered_map<std::string, ResourcePtr> m_resourceTemplates;
  std::unordered_map<ResourceCategory, std::vector<std::string>>
      m_categoryIndex;
  std::unordered_map<ResourceType, std::vector<std::string>> m_typeIndex;

  mutable ResourceStats m_stats;
  bool m_initialized{false};
  bool m_isShutdown{false};

  // Thread safety
  mutable std::shared_mutex m_resourceMutex;
  mutable std::mutex m_indexMutex;

  // Helper methods
  void updateIndexes(const std::string &resourceId, ResourceCategory category,
                     ResourceType type);
  void removeFromIndexes(const std::string &resourceId);
  void rebuildIndexes();
  bool isValidResourceId(const std::string &resourceId) const;

  // Default resource creation
  void createDefaultResources();
};

#endif // RESOURCE_TEMPLATE_MANAGER_HPP