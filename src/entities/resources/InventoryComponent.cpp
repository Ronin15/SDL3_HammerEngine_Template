/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/resources/InventoryComponent.hpp"
#include "core/Logger.hpp"
#include "entities/Entity.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include <algorithm>
#include <cassert>
#include <numeric>
#include <stdexcept>

InventoryComponent::InventoryComponent(Entity *owner, size_t maxSlots,
                                       const std::string &worldId)
    : m_owner(owner), m_maxSlots(maxSlots), m_onResourceChanged(nullptr),
      m_worldId(worldId), m_trackWorldResources(true) {

  m_slots.resize(m_maxSlots);

  // Initialize all slots as empty
  for (auto &slot : m_slots) {
    slot.clear();
  }

  RESOURCE_DEBUG("InventoryComponent created with " + std::to_string(maxSlots) +
                 " slots");
}

bool InventoryComponent::addResource(HammerEngine::ResourceHandle handle,
                                     int quantity) {
  if (quantity <= 0 || !handle.isValid()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  // Get resource template to check properties
  auto resourceTemplate =
      ResourceTemplateManager::Instance().getResourceTemplate(handle);
  if (!resourceTemplate) {
    RESOURCE_ERROR(
        "InventoryComponent::addResource - Invalid resource handle: " +
        handle.toString());
    return false;
  }

  int remainingQuantity = quantity;
  int oldQuantity = getResourceQuantityUnlocked(handle);
  // If resource is stackable, try to stack in existing slots first
  if (resourceTemplate->isStackable()) {
    for (auto &slot : m_slots) {
      if (slot.resourceHandle == handle &&
          slot.quantity < resourceTemplate->getMaxStackSize()) {
        int canAdd = resourceTemplate->getMaxStackSize() - slot.quantity;
        int toAdd = std::min(remainingQuantity, canAdd);

        slot.quantity += toAdd;
        remainingQuantity -= toAdd;

        if (remainingQuantity <= 0) {
          break;
        }
      }
    }
  }

  // If still have remaining quantity, find empty slots
  while (remainingQuantity > 0) {
    int emptySlot = findEmptySlot();
    if (emptySlot == -1) {
      // No more space - notify about partial success if any was added
      if (remainingQuantity < quantity) {
        int newQuantity = getResourceQuantityUnlocked(handle);
        notifyResourceChange(handle, oldQuantity, newQuantity);
      }
      return false;
    }

    int toAdd =
        resourceTemplate->isStackable()
            ? std::min(remainingQuantity, resourceTemplate->getMaxStackSize())
            : std::min(remainingQuantity, 1);

    m_slots[emptySlot] = InventorySlot(handle, toAdd);
    remainingQuantity -= toAdd;
  }

  // Notify about the change and update cache incrementally
  int newQuantity = getResourceQuantityUnlocked(handle);
  int actualChange = newQuantity - oldQuantity;
  updateQuantityCache(handle, actualChange);
  notifyResourceChange(handle, oldQuantity, newQuantity);

  return true;
}

bool InventoryComponent::removeResource(HammerEngine::ResourceHandle handle,
                                        int quantity) {
  if (quantity <= 0 || !handle.isValid()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  int oldQuantity = getResourceQuantityUnlocked(handle);
  if (oldQuantity < quantity) {
    return false; // Not enough resources
  }

  int remainingToRemove = quantity;
  // Remove from slots (starting from the end to avoid fragmentation)
  for (auto it = m_slots.rbegin();
       it != m_slots.rend() && remainingToRemove > 0; ++it) {
    if (it->resourceHandle == handle) {
      int toRemove = std::min(remainingToRemove, it->quantity);
      it->quantity -= toRemove;
      remainingToRemove -= toRemove;

      if (it->quantity <= 0) {
        it->clear();
      }
    }
  }

  // Notify about the change and update cache incrementally
  int newQuantity = getResourceQuantityUnlocked(handle);
  int actualChange = newQuantity - oldQuantity;
  updateQuantityCache(handle, actualChange);
  notifyResourceChange(handle, oldQuantity, newQuantity);

  return remainingToRemove == 0;
}

int InventoryComponent::getResourceQuantity(
    HammerEngine::ResourceHandle handle) const {
  std::lock_guard<std::mutex> lock(m_inventoryMutex);
  return std::accumulate(m_slots.begin(), m_slots.end(), 0,
                         [&handle](int sum, const InventorySlot &slot) {
                           return slot.resourceHandle == handle
                                      ? sum + slot.quantity
                                      : sum;
                         });
}

bool InventoryComponent::hasResource(HammerEngine::ResourceHandle handle,
                                     int minimumQuantity) const {
  return getResourceQuantity(handle) >= minimumQuantity;
}

void InventoryComponent::clearInventory() {
  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  // Notify about each resource being removed (build map here without calling
  // getAllResources)
  std::unordered_map<HammerEngine::ResourceHandle, int> resources;
  for (const auto &slot : m_slots) {
    if (!slot.isEmpty()) {
      resources[slot.resourceHandle] += slot.quantity;
    }
  }

  for (const auto &[resourceHandle, quantity] : resources) {
    notifyResourceChange(resourceHandle, quantity, 0);
  }

  // Clear all slots
  for (auto &slot : m_slots) {
    slot.clear();
  }

  // Clear the quantity cache and mark as clean
  m_resourceQuantityCache.clear();
  m_cacheNeedsRebuild = false;
}

size_t InventoryComponent::getUsedSlots() const {
  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  return std::count_if(
      m_slots.begin(), m_slots.end(),
      [](const InventorySlot &slot) { return !slot.isEmpty(); });
}

size_t InventoryComponent::getAvailableSlots() const {
  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  // Use inline calculation to avoid recursive mutex lock
  size_t usedSlots =
      std::count_if(m_slots.begin(), m_slots.end(),
                    [](const InventorySlot &slot) { return !slot.isEmpty(); });

  return m_maxSlots - usedSlots;
}

bool InventoryComponent::isFull() const { return getAvailableSlots() == 0; }

bool InventoryComponent::isEmpty() const { return getUsedSlots() == 0; }

std::vector<InventorySlot>
InventoryComponent::getResourcesByCategory(ResourceCategory category) const {
  std::lock_guard<std::mutex> lock(m_inventoryMutex);
  std::vector<InventorySlot> result;

  for (const auto &slot : m_slots) {
    if (!slot.isEmpty()) {
      auto resourceTemplate =
          ResourceTemplateManager::Instance().getResourceTemplate(
              slot.resourceHandle);
      if (resourceTemplate && resourceTemplate->getCategory() == category) {
        result.push_back(slot);
      }
    }
  }

  return result;
}

std::unordered_map<HammerEngine::ResourceHandle, int>
InventoryComponent::getAllResources() const {
  std::lock_guard<std::mutex> lock(m_inventoryMutex);
  std::unordered_map<HammerEngine::ResourceHandle, int> result;

  for (const auto &slot : m_slots) {
    if (!slot.isEmpty()) {
      result[slot.resourceHandle] += slot.quantity;
    }
  }

  return result;
}

const InventorySlot &InventoryComponent::getSlot(size_t slotIndex) const {
  validateSlotIndex(slotIndex);
  std::lock_guard<std::mutex> lock(m_inventoryMutex);
  return m_slots[slotIndex];
}

bool InventoryComponent::setSlot(size_t slotIndex,
                                 HammerEngine::ResourceHandle handle,
                                 int quantity) {
  validateSlotIndex(slotIndex);

  if (quantity < 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  // Get old values for notification
  HammerEngine::ResourceHandle oldHandle = m_slots[slotIndex].resourceHandle;
  int oldQuantity =
      oldHandle.isValid() ? getResourceQuantityUnlocked(oldHandle) : 0;

  // Set new slot value
  if (quantity == 0 || !handle.isValid()) {
    m_slots[slotIndex].clear();
  } else {
    m_slots[slotIndex] = InventorySlot(handle, quantity);
  }

  // Notify about changes and mark cache for rebuild (complex operation)
  m_cacheNeedsRebuild = true;
  if (oldHandle.isValid() && oldHandle != handle) {
    int newOldQuantity = getResourceQuantityUnlocked(oldHandle);
    notifyResourceChange(oldHandle, oldQuantity, newOldQuantity);
  }

  if (handle.isValid()) {
    int newQuantity = getResourceQuantityUnlocked(handle);
    notifyResourceChange(handle, oldQuantity, newQuantity);
  }

  return true;
}

bool InventoryComponent::transferTo(InventoryComponent &target,
                                    const std::string &resourceName,
                                    int quantity) {
  auto handle = findResourceByName(resourceName);
  if (!handle.isValid()) {
    return false;
  }
  return transferTo(target, handle, quantity);
}

void InventoryComponent::compactInventory() {
  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  // Remove empty slots and move non-empty slots to the front
  std::stable_partition(
      m_slots.begin(), m_slots.end(),
      [](const InventorySlot &slot) { return !slot.isEmpty(); });

  // Try to stack identical resources
  for (size_t i = 0; i < m_slots.size(); ++i) {
    if (m_slots[i].isEmpty())
      continue;

    auto resourceTemplate =
        ResourceTemplateManager::Instance().getResourceTemplate(
            m_slots[i].resourceHandle);
    if (!resourceTemplate || !resourceTemplate->isStackable())
      continue;

    for (size_t j = i + 1; j < m_slots.size(); ++j) {
      if (m_slots[j].resourceHandle == m_slots[i].resourceHandle) {
        int canAdd = resourceTemplate->getMaxStackSize() - m_slots[i].quantity;
        int toMove = std::min(canAdd, m_slots[j].quantity);

        m_slots[i].quantity += toMove;
        m_slots[j].quantity -= toMove;

        if (m_slots[j].quantity <= 0) {
          m_slots[j].clear();
        }

        if (m_slots[i].quantity >= resourceTemplate->getMaxStackSize()) {
          break;
        }
      }
    }
  }

  // Sort again to move empty slots to the end
  std::stable_partition(
      m_slots.begin(), m_slots.end(),
      [](const InventorySlot &slot) { return !slot.isEmpty(); });

  m_cacheNeedsRebuild = true;
}

// Helper methods implementation
int InventoryComponent::findSlotWithResource(
    HammerEngine::ResourceHandle handle) const {
  for (size_t i = 0; i < m_slots.size(); ++i) {
    if (m_slots[i].resourceHandle == handle) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int InventoryComponent::findEmptySlot() const {
  for (size_t i = 0; i < m_slots.size(); ++i) {
    if (m_slots[i].isEmpty()) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void InventoryComponent::notifyResourceChange(
    HammerEngine::ResourceHandle handle, int oldQuantity, int newQuantity) {
  if (oldQuantity != newQuantity) {
    // Update WorldResourceManager if tracking is enabled
    int quantityChange = newQuantity - oldQuantity;
    updateWorldResourceManager(handle, quantityChange);

    // Call the user-defined callback
    if (m_onResourceChanged) {
      m_onResourceChanged(handle, oldQuantity, newQuantity);
    }
  }
}

void InventoryComponent::validateSlotIndex(size_t slotIndex) const {
  if (slotIndex >= m_maxSlots) {
    throw std::out_of_range(
        "Slot index " + std::to_string(slotIndex) +
        " is out of range (max: " + std::to_string(m_maxSlots) + ")");
  }
}

void InventoryComponent::updateWorldResourceManager(
    HammerEngine::ResourceHandle handle, int quantityChange) {
  if (!m_trackWorldResources || quantityChange == 0) {
    return;
  }

  auto &worldResourceManager = WorldResourceManager::Instance();
  if (!worldResourceManager.isInitialized()) {
    return; // WorldResourceManager not available
  }

  if (!handle.isValid()) {
    RESOURCE_WARN("InventoryComponent::updateWorldResourceManager - Invalid "
                  "resource handle");
    return;
  }

  ResourceTransactionResult result;
  if (quantityChange > 0) {
    result =
        worldResourceManager.addResource(m_worldId, handle, quantityChange);
  } else {
    result =
        worldResourceManager.removeResource(m_worldId, handle, -quantityChange);
  }

  if (result != ResourceTransactionResult::Success) {
    // Get resource name for logging
    auto resourceTemplate =
        ResourceTemplateManager::Instance().getResourceTemplate(handle);
    std::string resourceName =
        resourceTemplate ? resourceTemplate->getName() : "Unknown Resource";
    RESOURCE_WARN("InventoryComponent::updateWorldResourceManager - Failed to "
                  "update world resources for " +
                  resourceName + " in world " + m_worldId);
  }
}

void InventoryComponent::updateQuantityCache(
    HammerEngine::ResourceHandle handle, int quantityChange) {
  if (m_cacheNeedsRebuild) {
    // If cache needs rebuilding anyway, don't bother with incremental updates
    return;
  }

  auto it = m_resourceQuantityCache.find(handle);
  if (it != m_resourceQuantityCache.end()) {
    it->second += quantityChange;
    if (it->second <= 0) {
      m_resourceQuantityCache.erase(it);
    }
  } else if (quantityChange > 0) {
    m_resourceQuantityCache[handle] = quantityChange;
  } else { // Cache miss with negative change - need to rebuild to get accurate
           // count
    m_cacheNeedsRebuild = true;
  }
}

void InventoryComponent::rebuildQuantityCache() const {
  m_resourceQuantityCache.clear();
  for (const auto &slot : m_slots) {
    if (!slot.isEmpty()) {
      m_resourceQuantityCache[slot.resourceHandle] += slot.quantity;
    }
  }
  m_cacheNeedsRebuild = false;
}

// String-based convenience methods implementation
HammerEngine::ResourceHandle
InventoryComponent::findResourceByName(const std::string &name) const {
  // ARCHITECTURAL FIX: Direct efficient lookup using ResourceTemplateManager
  auto &templateManager = ResourceTemplateManager::Instance();
  auto resource = templateManager.getResourceByName(name);
  return resource ? resource->getHandle()
                  : HammerEngine::INVALID_RESOURCE_HANDLE;
}
bool InventoryComponent::addResource(const std::string &resourceName,
                                     int quantity) {
  auto handle = findResourceByName(resourceName);
  if (!handle.isValid()) {
    RESOURCE_ERROR("InventoryComponent::addResource - Resource not found: " +
                   resourceName);
    return false;
  }
  return addResource(handle, quantity);
}

bool InventoryComponent::removeResource(const std::string &resourceName,
                                        int quantity) {
  auto handle = findResourceByName(resourceName);
  if (!handle.isValid()) {
    return false;
  }
  return removeResource(handle, quantity);
}

int InventoryComponent::getResourceQuantity(
    const std::string &resourceName) const {
  auto handle = findResourceByName(resourceName);
  if (!handle.isValid()) {
    return 0;
  }
  return getResourceQuantity(handle);
}

bool InventoryComponent::hasResource(const std::string &resourceName,
                                     int minimumQuantity) const {
  return getResourceQuantity(resourceName) >= minimumQuantity;
}

HammerEngine::ResourceHandle
InventoryComponent::getResourceHandle(const std::string &resourceName) const {
  return findResourceByName(resourceName);
}

int InventoryComponent::getResourceQuantityUnlocked(
    HammerEngine::ResourceHandle handle) const {
  return std::accumulate(m_slots.begin(), m_slots.end(), 0,
                         [&handle](int sum, const InventorySlot &slot) {
                           return slot.resourceHandle == handle
                                      ? sum + slot.quantity
                                      : sum;
                         });
}

std::vector<HammerEngine::ResourceHandle>
InventoryComponent::getResourceHandles() const {
  std::lock_guard<std::mutex> lock(m_inventoryMutex);
  std::vector<HammerEngine::ResourceHandle> handles;

  for (const auto &slot : m_slots) {
    if (!slot.isEmpty()) {
      handles.push_back(slot.resourceHandle);
    }
  }

  return handles;
}

bool InventoryComponent::canAddResource(HammerEngine::ResourceHandle handle,
                                        int quantity) const {
  if (quantity <= 0 || !handle.isValid()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  // Check if we already have this resource (can stack)
  for (const auto &slot : m_slots) {
    if (!slot.isEmpty() && slot.resourceHandle == handle) {
      auto resourceTemplate =
          ResourceTemplateManager::Instance().getResourceTemplate(handle);
      if (resourceTemplate && resourceTemplate->isStackable()) {
        return slot.quantity + quantity <= resourceTemplate->getMaxStackSize();
      }
    }
  }

  // Need a new slot - calculate available slots without calling
  // getAvailableSlots() to avoid recursive mutex lock
  size_t usedSlots =
      std::count_if(m_slots.begin(), m_slots.end(),
                    [](const InventorySlot &slot) { return !slot.isEmpty(); });

  return (m_maxSlots - usedSlots) > 0;
}

bool InventoryComponent::transferTo(InventoryComponent &target,
                                    HammerEngine::ResourceHandle handle,
                                    int quantity) {
  if (!handle.isValid() || quantity <= 0) {
    return false;
  }

  // Check if we have enough of the resource
  if (getResourceQuantity(handle) < quantity) {
    return false;
  }

  // Check if target can accept the resource
  if (!target.canAddResource(handle, quantity)) {
    return false;
  }

  // Perform the transfer
  if (removeResource(handle, quantity)) {
    if (target.addResource(handle, quantity)) {
      return true;
    } else {
      // Rollback if target couldn't add
      addResource(handle, quantity);
      return false;
    }
  }

  return false;
}
