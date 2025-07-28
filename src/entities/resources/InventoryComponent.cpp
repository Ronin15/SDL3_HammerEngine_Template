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

bool InventoryComponent::addResource(const std::string &resourceId,
                                     int quantity) {
  if (quantity <= 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  // Get resource template to check properties
  auto resourceTemplate =
      ResourceTemplateManager::Instance().getResourceTemplate(resourceId);
  if (!resourceTemplate) {
    RESOURCE_ERROR("InventoryComponent::addResource - Unknown resource: " +
                   resourceId);
    return false;
  }

  int remainingQuantity = quantity;
  int oldQuantity = getResourceQuantityUnlocked(resourceId);

  // If resource is stackable, try to stack in existing slots first
  if (resourceTemplate->isStackable()) {
    for (auto &slot : m_slots) {
      if (slot.resourceId == resourceId &&
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
        int newQuantity = getResourceQuantityUnlocked(resourceId);
        notifyResourceChange(resourceId, oldQuantity, newQuantity);
      }
      return false;
    }

    int toAdd =
        resourceTemplate->isStackable()
            ? std::min(remainingQuantity, resourceTemplate->getMaxStackSize())
            : std::min(remainingQuantity, 1);

    m_slots[emptySlot] = InventorySlot(resourceId, toAdd);
    remainingQuantity -= toAdd;
  }

  // Notify about the change and update cache incrementally
  int newQuantity = getResourceQuantityUnlocked(resourceId);
  int actualChange = newQuantity - oldQuantity;
  updateQuantityCache(resourceId, actualChange);
  notifyResourceChange(resourceId, oldQuantity, newQuantity);

  return true;
}

bool InventoryComponent::removeResource(const std::string &resourceId,
                                        int quantity) {
  if (quantity <= 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  int oldQuantity = getResourceQuantityUnlocked(resourceId);
  if (oldQuantity < quantity) {
    return false; // Not enough resources
  }

  int remainingToRemove = quantity;

  // Remove from slots (starting from the end to avoid fragmentation)
  for (auto it = m_slots.rbegin();
       it != m_slots.rend() && remainingToRemove > 0; ++it) {
    if (it->resourceId == resourceId) {
      int toRemove = std::min(remainingToRemove, it->quantity);
      it->quantity -= toRemove;
      remainingToRemove -= toRemove;

      if (it->quantity <= 0) {
        it->clear();
      }
    }
  }

  // Notify about the change and update cache incrementally
  int newQuantity = getResourceQuantityUnlocked(resourceId);
  int actualChange = newQuantity - oldQuantity;
  updateQuantityCache(resourceId, actualChange);
  notifyResourceChange(resourceId, oldQuantity, newQuantity);

  return remainingToRemove == 0;
}

int InventoryComponent::getResourceQuantity(
    const std::string &resourceId) const {
  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  return std::accumulate(m_slots.begin(), m_slots.end(), 0,
                         [&resourceId](int sum, const InventorySlot &slot) {
                           return slot.resourceId == resourceId
                                      ? sum + slot.quantity
                                      : sum;
                         });
}

bool InventoryComponent::hasResource(const std::string &resourceId,
                                     int minimumQuantity) const {
  return getResourceQuantity(resourceId) >= minimumQuantity;
}

void InventoryComponent::clearInventory() {
  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  // Notify about each resource being removed (build map here without calling
  // getAllResources)
  std::unordered_map<std::string, int> resources;
  for (const auto &slot : m_slots) {
    if (!slot.isEmpty()) {
      resources[slot.resourceId] += slot.quantity;
    }
  }

  for (const auto &[resourceId, quantity] : resources) {
    notifyResourceChange(resourceId, quantity, 0);
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
              slot.resourceId);
      if (resourceTemplate && resourceTemplate->getCategory() == category) {
        result.push_back(slot);
      }
    }
  }

  return result;
}

std::unordered_map<std::string, int>
InventoryComponent::getAllResources() const {
  std::lock_guard<std::mutex> lock(m_inventoryMutex);
  std::unordered_map<std::string, int> result;

  for (const auto &slot : m_slots) {
    if (!slot.isEmpty()) {
      result[slot.resourceId] += slot.quantity;
    }
  }

  return result;
}

std::vector<std::string> InventoryComponent::getResourceIds() const {
  std::lock_guard<std::mutex> lock(m_inventoryMutex);
  std::vector<std::string> result;

  for (const auto &slot : m_slots) {
    if (!slot.isEmpty()) {
      // Only add unique IDs
      if (std::find(result.begin(), result.end(), slot.resourceId) ==
          result.end()) {
        result.push_back(slot.resourceId);
      }
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
                                 const std::string &resourceId, int quantity) {
  validateSlotIndex(slotIndex);

  if (quantity < 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  // Get old values for notification
  std::string oldResourceId = m_slots[slotIndex].resourceId;
  int oldQuantity =
      oldResourceId.empty() ? 0 : getResourceQuantityUnlocked(oldResourceId);

  // Set new slot value
  if (quantity == 0 || resourceId.empty()) {
    m_slots[slotIndex].clear();
  } else {
    m_slots[slotIndex] = InventorySlot(resourceId, quantity);
  }

  // Notify about changes and mark cache for rebuild (complex operation)
  m_cacheNeedsRebuild = true;
  if (!oldResourceId.empty() && oldResourceId != resourceId) {
    int newOldQuantity = getResourceQuantityUnlocked(oldResourceId);
    notifyResourceChange(oldResourceId, oldQuantity, newOldQuantity);
  }

  if (!resourceId.empty()) {
    int newQuantity = getResourceQuantityUnlocked(resourceId);
    notifyResourceChange(resourceId, oldQuantity, newQuantity);
  }

  return true;
}

bool InventoryComponent::transferTo(InventoryComponent &target,
                                    const std::string &resourceId,
                                    int quantity) {
  if (!hasResource(resourceId, quantity)) {
    return false;
  }

  if (!target.canAddResource(resourceId, quantity)) {
    return false;
  }

  // Remove from source and add to target
  if (removeResource(resourceId, quantity)) {
    if (target.addResource(resourceId, quantity)) {
      return true;
    } else {
      // Rollback - add back to source
      addResource(resourceId, quantity);
      return false;
    }
  }

  return false;
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
            m_slots[i].resourceId);
    if (!resourceTemplate || !resourceTemplate->isStackable())
      continue;

    for (size_t j = i + 1; j < m_slots.size(); ++j) {
      if (m_slots[j].resourceId == m_slots[i].resourceId) {
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
}

// Helper methods implementation
int InventoryComponent::findSlotWithResource(
    const std::string &resourceId) const {
  for (size_t i = 0; i < m_slots.size(); ++i) {
    if (m_slots[i].resourceId == resourceId) {
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

void InventoryComponent::notifyResourceChange(const std::string &resourceId,
                                              int oldQuantity,
                                              int newQuantity) {
  if (oldQuantity != newQuantity) {
    // Update WorldResourceManager if tracking is enabled
    int quantityChange = newQuantity - oldQuantity;
    updateWorldResourceManager(resourceId, quantityChange);

    // Call the user-defined callback
    if (m_onResourceChanged) {
      m_onResourceChanged(resourceId, oldQuantity, newQuantity);
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

bool InventoryComponent::canAddResource(const std::string &resourceId,
                                        int quantity) const {
  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  auto resourceTemplate =
      ResourceTemplateManager::Instance().getResourceTemplate(resourceId);
  if (!resourceTemplate) {
    return false;
  }

  if (!resourceTemplate->isStackable()) {
    // Calculate available slots inline to avoid recursive mutex lock
    size_t usedSlots = std::count_if(
        m_slots.begin(), m_slots.end(),
        [](const InventorySlot &slot) { return !slot.isEmpty(); });
    size_t availableSlots = m_maxSlots - usedSlots;
    return availableSlots >= static_cast<size_t>(quantity);
  }

  // Validate stack size for stackable resources
  int maxStackSize = resourceTemplate->getMaxStackSize();
  if (maxStackSize <= 0) {
    RESOURCE_ERROR(
        "Stackable resource " + resourceId +
        " has invalid max stack size: " + std::to_string(maxStackSize));
    return false;
  }

  // Calculate how much can fit in existing stacks
  int canFitInExisting = std::accumulate(
      m_slots.begin(), m_slots.end(), 0,
      [&resourceId, maxStackSize](int sum, const InventorySlot &slot) {
        return slot.resourceId == resourceId
                   ? sum + (maxStackSize - slot.quantity)
                   : sum;
      });

  int remaining = quantity - canFitInExisting;
  if (remaining <= 0) {
    return true; // Can fit in existing stacks
  }

  // Calculate how many new stacks we need
  if (maxStackSize <= 0) {
    RESOURCE_ERROR("Resource " + resourceId + " has invalid max stack size: " +
                   std::to_string(maxStackSize));
    return false;
  }
  int stacksNeeded = (remaining + maxStackSize - 1) / maxStackSize;

  // Calculate available slots inline to avoid recursive mutex lock
  size_t usedSlots =
      std::count_if(m_slots.begin(), m_slots.end(),
                    [](const InventorySlot &slot) { return !slot.isEmpty(); });
  size_t availableSlots = m_maxSlots - usedSlots;

  return availableSlots >= static_cast<size_t>(stacksNeeded);
}

int InventoryComponent::getResourceQuantityUnlocked(
    const std::string &resourceId) const {
  // If cache needs rebuilding, rebuild it once
  if (m_cacheNeedsRebuild) {
    rebuildQuantityCache();
    m_cacheNeedsRebuild = false;
  }

  // Use cache for O(1) lookup
  auto it = m_resourceQuantityCache.find(resourceId);
  return (it != m_resourceQuantityCache.end()) ? it->second : 0;
}
void InventoryComponent::updateWorldResourceManager(
    const std::string &resourceId, int quantityChange) {
  if (!m_trackWorldResources || quantityChange == 0) {
    return;
  }

  auto &worldResourceManager = WorldResourceManager::Instance();
  if (!worldResourceManager.isInitialized()) {
    return; // WorldResourceManager not available
  }

  ResourceTransactionResult result;
  if (quantityChange > 0) {
    result =
        worldResourceManager.addResource(m_worldId, resourceId, quantityChange);
  } else {
    result = worldResourceManager.removeResource(m_worldId, resourceId,
                                                 -quantityChange);
  }

  if (result != ResourceTransactionResult::Success) {
    RESOURCE_WARN("InventoryComponent::updateWorldResourceManager - Failed to "
                  "update world resources for " +
                  resourceId + " in world " + m_worldId);
  }
}

void InventoryComponent::updateQuantityCache(const std::string &resourceId,
                                             int quantityChange) {
  if (m_cacheNeedsRebuild) {
    // If cache needs rebuilding anyway, don't bother with incremental updates
    return;
  }

  auto it = m_resourceQuantityCache.find(resourceId);
  if (it != m_resourceQuantityCache.end()) {
    it->second += quantityChange;
    if (it->second <= 0) {
      m_resourceQuantityCache.erase(it);
    }
  } else if (quantityChange > 0) {
    m_resourceQuantityCache[resourceId] = quantityChange;
  } else {
    // Cache miss with negative change - need to rebuild to get accurate count
    m_cacheNeedsRebuild = true;
  }
}

void InventoryComponent::rebuildQuantityCache() const {
  m_resourceQuantityCache.clear();
  for (const auto &slot : m_slots) {
    if (!slot.isEmpty()) {
      m_resourceQuantityCache[slot.resourceId] += slot.quantity;
    }
  }
  m_cacheNeedsRebuild = false;
}