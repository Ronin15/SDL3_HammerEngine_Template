/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/ResourceTemplateManager.hpp"
#include "core/Logger.hpp"
#include "managers/ResourceFactory.hpp"
#include "utils/JsonReader.hpp"
#include <algorithm>

using HammerEngine::JsonReader;
using HammerEngine::JsonValue;
using HammerEngine::ResourceFactory;

ResourceTemplateManager &ResourceTemplateManager::Instance() {
  static ResourceTemplateManager instance;
  return instance;
}

ResourceTemplateManager::~ResourceTemplateManager() {
  // Only clean up if not already shut down
  if (!m_isShutdown) {
    clean();
  }
}

bool ResourceTemplateManager::init() {
  if (m_initialized) {
    return true; // Already initialized
  }

  std::lock_guard<std::shared_mutex> lock(m_resourceMutex);

  if (m_initialized) {
    return true; // Double-check after acquiring lock
  }

  try {
    // Clear any existing data
    m_resourceTemplates.clear();
    m_categoryIndex.clear();
    m_typeIndex.clear();

    // Initialize category index
    for (int i = 0; i < static_cast<int>(ResourceCategory::COUNT); ++i) {
      m_categoryIndex[static_cast<ResourceCategory>(i)] =
          std::vector<std::string>();
    }

    // Initialize type index
    for (int i = 0; i < static_cast<int>(ResourceType::COUNT); ++i) {
      m_typeIndex[static_cast<ResourceType>(i)] = std::vector<std::string>();
    }

    // Initialize ResourceFactory
    ResourceFactory::initialize();

    // Create default resources
    createDefaultResources();

    m_initialized = true;
    m_isShutdown = false; // Reset shutdown flag on successful init
    m_stats.reset();
    RESOURCE_INFO("ResourceTemplateManager initialized with " +
                  std::to_string(m_resourceTemplates.size()) +
                  " resource templates");

    return true;
  } catch (const std::exception &ex) {
    RESOURCE_ERROR("ResourceTemplateManager::init - Exception: " +
                   std::string(ex.what()));
    return false;
  }
}

void ResourceTemplateManager::clean() {
  std::lock_guard<std::shared_mutex> lock(m_resourceMutex);

  // Clear all data structures
  m_resourceTemplates.clear();
  m_categoryIndex.clear();
  m_typeIndex.clear();

  m_initialized = false;
  m_isShutdown = true;
  m_stats.reset();

  RESOURCE_INFO("ResourceTemplateManager cleaned up");
}
bool ResourceTemplateManager::registerResourceTemplate(
    const ResourcePtr &resource) {
  if (!resource) {
    RESOURCE_ERROR("ResourceTemplateManager::registerResourceTemplate - Null "
                   "resource provided");
    return false;
  }

  if (!isValidResourceId(resource->getId())) {
    RESOURCE_ERROR("ResourceTemplateManager::registerResourceTemplate - "
                   "Invalid resource ID: " +
                   resource->getId());
    return false;
  }

  std::lock_guard<std::shared_mutex> lock(m_resourceMutex);

  const std::string &resourceId = resource->getId();

  // Check if already registered
  if (m_resourceTemplates.find(resourceId) != m_resourceTemplates.end()) {
    RESOURCE_WARN(
        "ResourceTemplateManager::registerResourceTemplate - Resource "
        "already registered: " +
        resourceId);
    return false;
  }

  try {
    // Register the resource template
    m_resourceTemplates[resourceId] = resource;

    // Update indexes
    updateIndexes(resourceId, resource->getCategory(), resource->getType());

    m_stats.templatesLoaded.fetch_add(1, std::memory_order_relaxed);

    RESOURCE_DEBUG(
        "ResourceTemplateManager::registerResourceTemplate - Registered: " +
        resourceId);
    return true;
  } catch (const std::exception &ex) {
    RESOURCE_ERROR(
        "ResourceTemplateManager::registerResourceTemplate - Exception: " +
        std::string(ex.what()));
    return false;
  }
}

ResourcePtr ResourceTemplateManager::getResourceTemplate(
    const std::string &resourceId) const {
  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);

  auto it = m_resourceTemplates.find(resourceId);
  if (it != m_resourceTemplates.end()) {
    return it->second;
  }

  return nullptr;
}

std::vector<ResourcePtr> ResourceTemplateManager::getResourcesByCategory(
    ResourceCategory category) const {
  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);
  std::vector<ResourcePtr> result;

  auto categoryIt = m_categoryIndex.find(category);
  if (categoryIt != m_categoryIndex.end()) {
    result.reserve(categoryIt->second.size());

    for (const auto &resourceId : categoryIt->second) {
      auto resourceIt = m_resourceTemplates.find(resourceId);
      if (resourceIt != m_resourceTemplates.end()) {
        result.push_back(resourceIt->second);
      }
    }
  }

  return result;
}

std::vector<ResourcePtr>
ResourceTemplateManager::getResourcesByType(ResourceType type) const {
  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);
  std::vector<ResourcePtr> result;

  auto typeIt = m_typeIndex.find(type);
  if (typeIt != m_typeIndex.end()) {
    result.reserve(typeIt->second.size());

    for (const auto &resourceId : typeIt->second) {
      auto resourceIt = m_resourceTemplates.find(resourceId);
      if (resourceIt != m_resourceTemplates.end()) {
        result.push_back(resourceIt->second);
      }
    }
  }

  return result;
}

ResourceStats ResourceTemplateManager::getStats() const { return m_stats; }
ResourcePtr
ResourceTemplateManager::createResource(const std::string &resourceId) const {
  auto templateResource = getResourceTemplate(resourceId);
  if (!templateResource) {
    RESOURCE_ERROR(
        "ResourceTemplateManager::createResource - Unknown resource: " +
        resourceId);
    return nullptr;
  }

  try {
    // Create a copy of the template
    ResourcePtr newResource = std::make_shared<Resource>(*templateResource);

    m_stats.resourcesCreated.fetch_add(1, std::memory_order_relaxed);

    return newResource;
  } catch (const std::exception &ex) {
    RESOURCE_ERROR("ResourceTemplateManager::createResource - Exception: " +
                   std::string(ex.what()));
    return nullptr;
  }
}

bool ResourceTemplateManager::loadResourcesFromJson(
    const std::string &filename) {
  JsonReader reader;
  if (!reader.loadFromFile(filename)) {
    RESOURCE_ERROR("ResourceTemplateManager::loadResourcesFromJson - Failed to "
                   "load file: " +
                   filename + " - " + reader.getLastError());
    return false;
  }

  return loadResourcesFromJsonString(reader.getRoot().toString());
}

bool ResourceTemplateManager::loadResourcesFromJsonString(
    const std::string &jsonString) {
  JsonReader reader;
  if (!reader.parse(jsonString)) {
    RESOURCE_ERROR("ResourceTemplateManager::loadResourcesFromJsonString - "
                   "Failed to parse JSON: " +
                   reader.getLastError());
    return false;
  }

  const JsonValue &root = reader.getRoot();

  if (!root.isObject()) {
    RESOURCE_ERROR("ResourceTemplateManager::loadResourcesFromJsonString - "
                   "Root JSON is not an object");
    return false;
  }

  // Check if we have a resources array
  if (!root.hasKey("resources") || !root["resources"].isArray()) {
    RESOURCE_ERROR("ResourceTemplateManager::loadResourcesFromJsonString - "
                   "Missing or invalid 'resources' array");
    return false;
  }

  const JsonValue &resourcesArray = root["resources"];
  size_t loadedCount = 0;
  size_t failedCount = 0;

  RESOURCE_INFO(
      "ResourceTemplateManager::loadResourcesFromJsonString - Loading " +
      std::to_string(resourcesArray.size()) + " resources from JSON");

  // Process each resource in the array
  for (size_t i = 0; i < resourcesArray.size(); ++i) {
    const JsonValue &resourceJson = resourcesArray[i];

    try {
      ResourcePtr resource = ResourceFactory::createFromJson(resourceJson);
      if (resource) {
        if (registerResourceTemplate(resource)) {
          loadedCount++;
          RESOURCE_DEBUG("ResourceTemplateManager::loadResourcesFromJsonString "
                         "- Loaded resource: " +
                         resource->getId());
        } else {
          failedCount++;
          RESOURCE_WARN("ResourceTemplateManager::loadResourcesFromJsonString "
                        "- Failed to register resource: " +
                        resource->getId());
        }
      } else {
        failedCount++;
        RESOURCE_WARN("ResourceTemplateManager::loadResourcesFromJsonString - "
                      "Failed to create resource from JSON at index " +
                      std::to_string(i));
      }
    } catch (const std::exception &ex) {
      failedCount++;
      RESOURCE_ERROR("ResourceTemplateManager::loadResourcesFromJsonString - "
                     "Exception processing resource at index " +
                     std::to_string(i) + ": " + std::string(ex.what()));
    }
  }

  RESOURCE_INFO(
      "ResourceTemplateManager::loadResourcesFromJsonString - Completed: " +
      std::to_string(loadedCount) + " loaded, " + std::to_string(failedCount) +
      " failed");

  return failedCount ==
         0; // Return true only if all resources were loaded successfully
}

size_t ResourceTemplateManager::getResourceTemplateCount() const {
  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);
  return m_resourceTemplates.size();
}

bool ResourceTemplateManager::hasResourceTemplate(
    const std::string &resourceId) const {
  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);
  return m_resourceTemplates.find(resourceId) != m_resourceTemplates.end();
}

size_t ResourceTemplateManager::getMemoryUsage() const {
  std::shared_lock<std::shared_mutex> lock(m_resourceMutex);

  size_t totalSize = 0;

  // Account for m_resourceTemplates map itself
  totalSize +=
      m_resourceTemplates.size() * (sizeof(std::string) + sizeof(ResourcePtr));

  // Account for the actual strings and Resource objects in m_resourceTemplates
  for (const auto &[resourceId, resource] : m_resourceTemplates) {
    totalSize += resourceId.size(); // Size of the key string
    if (resource) {
      totalSize += sizeof(Resource);           // Base Resource object size
      totalSize += resource->getName().size(); // Resource name
      totalSize += resource->getDescription().size(); // Resource description
    }
  }

  for (const auto &[categoryKey, resourceIds] : m_categoryIndex) {
    totalSize += resourceIds.size() * sizeof(std::string);
    for (const auto &id : resourceIds) {
      totalSize += id.size();
    }
  }

  for (const auto &[typeKey, resourceIds] : m_typeIndex) {
    totalSize += resourceIds.size() * sizeof(std::string);
    for (const auto &id : resourceIds) {
      totalSize += id.size();
    }
  }

  return totalSize;
}

void ResourceTemplateManager::updateIndexes(const std::string &resourceId,
                                            ResourceCategory category,
                                            ResourceType type) {
  std::lock_guard<std::mutex> lock(m_indexMutex);

  // Add to category index
  m_categoryIndex[category].push_back(resourceId);

  // Add to type index
  m_typeIndex[type].push_back(resourceId);
}

void ResourceTemplateManager::removeFromIndexes(const std::string &resourceId) {
  std::lock_guard<std::mutex> lock(m_indexMutex);

  // Remove from category index
  for (auto &[category, resourceIds] : m_categoryIndex) {
    auto it = std::find(resourceIds.begin(), resourceIds.end(), resourceId);
    if (it != resourceIds.end()) {
      resourceIds.erase(it);
    }
  }

  // Remove from type index
  for (auto &[type, resourceIds] : m_typeIndex) {
    auto it = std::find(resourceIds.begin(), resourceIds.end(), resourceId);
    if (it != resourceIds.end()) {
      resourceIds.erase(it);
    }
  }
}

void ResourceTemplateManager::rebuildIndexes() {
  std::lock_guard<std::mutex> lock(m_indexMutex);

  // Clear existing indexes
  m_categoryIndex.clear();
  m_typeIndex.clear();

  // Initialize empty vectors for all categories and types
  for (int i = 0; i < static_cast<int>(ResourceCategory::COUNT); ++i) {
    m_categoryIndex[static_cast<ResourceCategory>(i)] =
        std::vector<std::string>();
  }

  for (int i = 0; i < static_cast<int>(ResourceType::COUNT); ++i) {
    m_typeIndex[static_cast<ResourceType>(i)] = std::vector<std::string>();
  }

  // Rebuild indexes from current resources
  for (const auto &[resourceId, resource] : m_resourceTemplates) {
    if (resource) {
      m_categoryIndex[resource->getCategory()].push_back(resourceId);
      m_typeIndex[resource->getType()].push_back(resourceId);
    }
  }
}

bool ResourceTemplateManager::isValidResourceId(
    const std::string &resourceId) const {
  return !resourceId.empty() && resourceId.length() <= 64 && // Reasonable limit
         std::all_of(resourceId.begin(), resourceId.end(), [](char c) {
           return std::isalnum(c) || c == '_' || c == '-';
         });
}

void ResourceTemplateManager::createDefaultResources() {
  RESOURCE_INFO(
      "ResourceTemplateManager::createDefaultResources - Creating default "
      "resource templates");

  try {
    // Create basic items using base Resource class
    auto sword = std::make_shared<Resource>(
        "sword", "Iron Sword", ResourceCategory::Item, ResourceType::Equipment);
    sword->setValue(100);
    sword->setMaxStackSize(1); // Weapons don't stack
    sword->setConsumable(false);
    sword->setDescription("A sturdy iron sword with a sharp blade.");

    auto shield = std::make_shared<Resource>("shield", "Wooden Shield",
                                             ResourceCategory::Item,
                                             ResourceType::Equipment);
    shield->setValue(75);
    shield->setMaxStackSize(1); // Armor doesn't stack
    shield->setConsumable(false);
    shield->setDescription("A reliable wooden shield for basic protection.");

    auto potion = std::make_shared<Resource>("health_potion", "Health Potion",
                                             ResourceCategory::Item,
                                             ResourceType::Consumable);
    potion->setValue(50);
    potion->setMaxStackSize(10); // Potions can stack
    potion->setConsumable(true);
    potion->setDescription(
        "A magical potion that restores health when consumed.");

    // Register resources directly (we already have the lock in init())
    m_resourceTemplates["sword"] = sword;
    updateIndexes("sword", sword->getCategory(), sword->getType());
    m_stats.templatesLoaded.fetch_add(1, std::memory_order_relaxed);

    m_resourceTemplates["shield"] = shield;
    updateIndexes("shield", shield->getCategory(), shield->getType());
    m_stats.templatesLoaded.fetch_add(1, std::memory_order_relaxed);

    m_resourceTemplates["health_potion"] = potion;
    updateIndexes("health_potion", potion->getCategory(), potion->getType());
    m_stats.templatesLoaded.fetch_add(1, std::memory_order_relaxed);

    // Create default materials
    auto iron = std::make_shared<Resource>("iron_ore", "Iron Ore",
                                           ResourceCategory::Material,
                                           ResourceType::RawResource);
    iron->setValue(25);
    iron->setMaxStackSize(100); // Materials stack well
    iron->setConsumable(false);
    iron->setDescription("Raw iron ore that can be smelted into ingots.");

    auto wood = std::make_shared<Resource>("wood", "Oak Wood",
                                           ResourceCategory::Material,
                                           ResourceType::RawResource);
    wood->setValue(10);
    wood->setMaxStackSize(100);
    wood->setConsumable(false);
    wood->setDescription("High-quality oak wood useful for crafting.");

    m_resourceTemplates["iron_ore"] = iron;
    updateIndexes("iron_ore", iron->getCategory(), iron->getType());
    m_stats.templatesLoaded.fetch_add(1, std::memory_order_relaxed);

    m_resourceTemplates["wood"] = wood;
    updateIndexes("wood", wood->getCategory(), wood->getType());
    m_stats.templatesLoaded.fetch_add(1, std::memory_order_relaxed);

    // Create default currency
    auto gold = std::make_shared<Resource>(
        "gold", "Gold Coins", ResourceCategory::Currency, ResourceType::Gold);
    gold->setValue(1);
    gold->setMaxStackSize(10000); // Currency stacks high
    gold->setConsumable(false);
    gold->setDescription(
        "Shiny gold coins, the standard currency of the realm.");

    m_resourceTemplates["gold"] = gold;
    updateIndexes("gold", gold->getCategory(), gold->getType());
    m_stats.templatesLoaded.fetch_add(1, std::memory_order_relaxed);

    // Create default game resources
    auto xp = std::make_shared<Resource>("experience", "Experience Points",
                                         ResourceCategory::GameResource,
                                         ResourceType::Energy);
    xp->setValue(0);
    xp->setMaxStackSize(999999); // Experience can accumulate
    xp->setConsumable(false);
    xp->setDescription("Experience points gained through various activities.");

    m_resourceTemplates["experience"] = xp;
    updateIndexes("experience", xp->getCategory(), xp->getType());
    m_stats.templatesLoaded.fetch_add(1, std::memory_order_relaxed);

    // Add iron_sword for consistency with tests
    auto ironSword = std::make_shared<Resource>("iron_sword", "Iron Sword",
                                                ResourceCategory::Item,
                                                ResourceType::Equipment);
    ironSword->setValue(120);
    ironSword->setMaxStackSize(1);
    ironSword->setConsumable(false);
    ironSword->setDescription(
        "A well-crafted iron sword with excellent balance.");

    m_resourceTemplates["iron_sword"] = ironSword;
    updateIndexes("iron_sword", ironSword->getCategory(), ironSword->getType());
    m_stats.templatesLoaded.fetch_add(1, std::memory_order_relaxed);

    RESOURCE_INFO("ResourceTemplateManager::createDefaultResources - Created " +
                  std::to_string(m_resourceTemplates.size()) +
                  " default resources");

  } catch (const std::exception &ex) {
    RESOURCE_ERROR(
        "ResourceTemplateManager::createDefaultResources - Exception: " +
        std::string(ex.what()));
  }
}