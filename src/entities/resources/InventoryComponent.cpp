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
#include <format>
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

  RESOURCE_DEBUG(std::format("InventoryComponent initialized with {} slots", maxSlots));
}

bool InventoryComponent::addResource(HammerEngine::ResourceHandle handle,
                                     int quantity) {
  if (quantity <= 0 || !handle.isValid()) {
    return false;
  }

  // Check for overflow before processing
  if (!isValidQuantity(quantity)) {
    INVENTORY_ERROR(
        std::format("addResource - Invalid quantity: {} (max allowed: {}) for handle: {}",
                    quantity, MAX_SAFE_QUANTITY, handle.toString()));
    return false;
  }

  // Variables for callback (outside lock scope)
  int oldQuantity, newQuantity;
  bool success;

  {
    std::lock_guard<std::mutex> lock(m_inventoryMutex);

    // Get resource template to check properties
    auto resourceTemplate =
        ResourceTemplateManager::Instance().getResourceTemplate(handle);
    if (!resourceTemplate) {
      INVENTORY_ERROR(
          std::format("addResource - Invalid resource handle: {} - template not found in ResourceTemplateManager",
                      handle.toString()));
      return false;
    }

    int remainingQuantity = quantity;
    oldQuantity = getResourceQuantityUnlocked(handle);

    // Check for overflow in total quantity
    if (wouldOverflow(oldQuantity, quantity)) {
      INVENTORY_ERROR(
          std::format("addResource - Would cause overflow: {} + {} exceeds maximum safe quantity for handle: {}",
                      oldQuantity, quantity, handle.toString()));
      return false;
    }

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
        // No more space - partially successful but notify about what was added
        break;
      }

      int toAdd =
          resourceTemplate->isStackable()
              ? std::min(remainingQuantity, resourceTemplate->getMaxStackSize())
              : std::min(remainingQuantity, 1);

      // Normalize emptySlot to size_t and ensure in-bounds without throwing
      size_t sidx = emptySlot < 0 ? size_t{0} : static_cast<size_t>(emptySlot);
      if (sidx < m_slots.size()) {
        m_slots[sidx] = InventorySlot(handle, toAdd);
        remainingQuantity -= toAdd;
      } else {
        INVENTORY_WARN(std::format("Empty slot index {} out of bounds (slots size: {})", emptySlot, m_slots.size()));
        break;
      }
    }

    // Update cache and get final quantities
    newQuantity = getResourceQuantityUnlocked(handle);
    int actualChange = newQuantity - oldQuantity;
    updateQuantityCache(handle, actualChange);

    // Update WorldResourceManager if tracking is enabled
    if (actualChange != 0) {
      updateWorldResourceManager(handle, actualChange);
    }

    success = (remainingQuantity == 0);
  } // Release lock here

  // Invoke callback outside of lock to prevent deadlocks
  if (oldQuantity != newQuantity) {
    notifyResourceChangeSafe(handle, oldQuantity, newQuantity);
  }

  return success;
}

bool InventoryComponent::removeResource(HammerEngine::ResourceHandle handle,
                                        int quantity) {
  if (quantity <= 0 || !handle.isValid()) {
    return false;
  }

  // Check for invalid quantity
  if (!isValidQuantity(quantity)) {
    INVENTORY_ERROR(
        std::format("removeResource - Invalid quantity: {} for handle: {}",
                    quantity, handle.toString()));
    return false;
  }

  // Variables for callback (outside lock scope)
  int oldQuantity, newQuantity;
  bool success;

  {
    std::lock_guard<std::mutex> lock(m_inventoryMutex);

    oldQuantity = getResourceQuantityUnlocked(handle);

    // Check for underflow
    if (wouldUnderflow(oldQuantity, quantity)) {
      INVENTORY_WARN(std::format("removeResource - Would cause underflow: {} - {} is less than minimum for handle: {}",
                                  oldQuantity, quantity, handle.toString()));
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

    // Update cache and get final quantities
    newQuantity = getResourceQuantityUnlocked(handle);
    int actualChange = newQuantity - oldQuantity;
    updateQuantityCache(handle, actualChange);

    // Update WorldResourceManager if tracking is enabled
    if (actualChange != 0) {
      updateWorldResourceManager(handle, actualChange);
    }

    success = (remainingToRemove == 0);
  } // Release lock here

  // Invoke callback outside of lock to prevent deadlocks
  if (oldQuantity != newQuantity) {
    notifyResourceChangeSafe(handle, oldQuantity, newQuantity);
  }

  return success;
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
  // Collect resource quantities for notifications (outside lock scope)
  std::unordered_map<HammerEngine::ResourceHandle, int> resourcesToNotify;

  {
    std::lock_guard<std::mutex> lock(m_inventoryMutex);

    // Build map of resources to notify about
    for (const auto &slot : m_slots) {
      if (!slot.isEmpty()) {
        resourcesToNotify[slot.resourceHandle] += slot.quantity;
      }
    }

    // Update WorldResourceManager for each resource
    for (const auto &[resourceHandle, quantity] : resourcesToNotify) {
      updateWorldResourceManager(resourceHandle, -quantity);
    }

    // Clear all slots
    for (auto &slot : m_slots) {
      slot.clear();
    }

    // Clear the quantity cache and mark as clean
    m_resourceQuantityCache.clear();
    m_cacheNeedsRebuild = false;
  } // Release lock here

  // Invoke callbacks outside of lock to prevent deadlocks
  for (const auto &[resourceHandle, quantity] : resourcesToNotify) {
    notifyResourceChangeSafe(resourceHandle, quantity, 0);
  }
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

  if (quantity < 0 || !isValidQuantity(quantity)) {
    INVENTORY_ERROR(std::format("setSlot - Invalid quantity: {} for slot {} with handle: {}",
                                 quantity, slotIndex, handle.toString()));
    return false;
  }

  // Variables for callbacks (outside lock scope)
  HammerEngine::ResourceHandle oldHandle;
  int oldHandleOldQuantity = 0, oldHandleNewQuantity = 0;
  int newHandleOldQuantity = 0, newHandleNewQuantity = 0;
  bool needOldHandleCallback = false, needNewHandleCallback = false;

  {
    std::lock_guard<std::mutex> lock(m_inventoryMutex);

    // Get old values for notification
    oldHandle = m_slots[slotIndex].resourceHandle;
    if (oldHandle.isValid()) {
      oldHandleOldQuantity = getResourceQuantityUnlocked(oldHandle);
    }

    if (handle.isValid()) {
      newHandleOldQuantity = getResourceQuantityUnlocked(handle);
    }

    // Set new slot value
    if (quantity == 0 || !handle.isValid()) {
      m_slots[slotIndex].clear();
    } else {
      m_slots[slotIndex] = InventorySlot(handle, quantity);
    }

    // Mark cache for rebuild (complex operation involving multiple resources)
    m_cacheNeedsRebuild = true;

    // Calculate new quantities and update WorldResourceManager
    if (oldHandle.isValid() && oldHandle != handle) {
      oldHandleNewQuantity = getResourceQuantityUnlocked(oldHandle);
      int oldChange = oldHandleNewQuantity - oldHandleOldQuantity;
      if (oldChange != 0) {
        updateWorldResourceManager(oldHandle, oldChange);
        needOldHandleCallback = true;
      }
    }

    if (handle.isValid()) {
      newHandleNewQuantity = getResourceQuantityUnlocked(handle);
      int newChange = newHandleNewQuantity - newHandleOldQuantity;
      if (newChange != 0) {
        updateWorldResourceManager(handle, newChange);
        needNewHandleCallback = true;
      }
    }
  } // Release lock here

  // Invoke callbacks outside of lock to prevent deadlocks
  if (needOldHandleCallback) {
    notifyResourceChangeSafe(oldHandle, oldHandleOldQuantity,
                             oldHandleNewQuantity);
  }

  if (needNewHandleCallback) {
    notifyResourceChangeSafe(handle, newHandleOldQuantity,
                             newHandleNewQuantity);
  }

  return true;
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
  // This method is now primarily for backward compatibility and
  // WorldResourceManager For new code, use the direct approach of calling
  // updateWorldResourceManager and notifyResourceChangeSafe separately for
  // better thread safety
  if (oldQuantity != newQuantity) {
    // Update WorldResourceManager if tracking is enabled
    int quantityChange = newQuantity - oldQuantity;
    updateWorldResourceManager(handle, quantityChange);

    // Call the user-defined callback safely (without holding lock)
    notifyResourceChangeSafe(handle, oldQuantity, newQuantity);
  }
}

void InventoryComponent::notifyResourceChangeSafe(
    HammerEngine::ResourceHandle handle, int oldQuantity, int newQuantity) {
  // This method should be called when NOT holding the inventory mutex
  // to prevent deadlocks in callbacks
  if (m_onResourceChanged) {
    m_onResourceChanged(handle, oldQuantity, newQuantity);
  }
}

void InventoryComponent::validateSlotIndex(size_t slotIndex) const {
  if (slotIndex >= m_maxSlots) {
    throw std::out_of_range(
        std::format("Slot index {} is out of range (max: {})", slotIndex, m_maxSlots));
  }
}

void InventoryComponent::updateWorldResourceManager(
    HammerEngine::ResourceHandle handle, int quantityChange) const {
  if (!m_trackWorldResources || quantityChange == 0) {
    return;
  }

  auto &worldResourceManager = WorldResourceManager::Instance();
  if (!worldResourceManager.isInitialized()) {
    return; // WorldResourceManager not available
  }

  if (!handle.isValid()) {
    INVENTORY_WARN(std::format("updateWorldResourceManager - Invalid resource handle: {}",
                   handle.toString()));
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
    INVENTORY_WARN(
        std::format("updateWorldResourceManager - Failed to update world resources for {} (handle: {}) in world {} - Transaction result: {}",
                    resourceName, handle.toString(), m_worldId, static_cast<int>(result)));
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

// String-based convenience methods removed - use ResourceHandle only

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

// Quantity validation and overflow/underflow checks
bool InventoryComponent::isValidQuantity(int quantity) const {
  return quantity >= MIN_SAFE_QUANTITY && quantity <= MAX_SAFE_QUANTITY;
}

bool InventoryComponent::wouldOverflow(int currentQuantity,
                                       int addQuantity) const {
  // Check if addition would exceed maximum safe quantity or cause integer
  // overflow
  if (addQuantity > MAX_SAFE_QUANTITY - currentQuantity) {
    return true;
  }

  // Check for integer overflow using safe arithmetic
  if (currentQuantity > 0 && addQuantity > INT_MAX - currentQuantity) {
    return true;
  }

  return false;
}

bool InventoryComponent::wouldUnderflow(int currentQuantity,
                                        int removeQuantity) const {
  // Check if subtraction would go below minimum or cause underflow
  if (removeQuantity > currentQuantity) {
    return true;
  }

  // Check for integer underflow
  if (currentQuantity < 0 && removeQuantity > currentQuantity - INT_MIN) {
    return true;
  }

  return false;
}

// Diagnostic and error recovery methods
bool InventoryComponent::validateInventoryIntegrity() const {
  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  bool isValid = true;
  size_t issueCount = 0;

  INVENTORY_DEBUG("Starting inventory integrity validation");

  // Check slot consistency
  for (size_t i = 0; i < m_slots.size(); ++i) {
    const auto &slot = m_slots[i];

    // Check for invalid quantities
    if (!slot.isEmpty() && !isValidQuantity(slot.quantity)) {
      INVENTORY_ERROR(
          std::format("validateInventoryIntegrity - Slot {} has invalid quantity: {} for handle: {}",
                      i, slot.quantity, slot.resourceHandle.toString()));
      isValid = false;
      issueCount++;
    }

    // Check for invalid handles with non-zero quantities
    if (!slot.isEmpty() && !slot.resourceHandle.isValid()) {
      INVENTORY_ERROR(std::format("validateInventoryIntegrity - Slot {} has invalid handle but non-zero quantity: {}",
                                   i, slot.quantity));
      isValid = false;
      issueCount++;
    }

    // Verify resource template exists for used slots
    if (!slot.isEmpty()) {
      auto resourceTemplate =
          ResourceTemplateManager::Instance().getResourceTemplate(
              slot.resourceHandle);
      if (!resourceTemplate) {
        INVENTORY_ERROR(
            std::format("validateInventoryIntegrity - Slot {} references non-existent resource template for handle: {}",
                        i, slot.resourceHandle.toString()));
        isValid = false;
        issueCount++;
      } else {
        // Check if quantity exceeds stack size for stackable resources
        if (resourceTemplate->isStackable() &&
            slot.quantity > resourceTemplate->getMaxStackSize()) {
          INVENTORY_ERROR(std::format("validateInventoryIntegrity - Slot {} quantity {} exceeds max stack size {} for handle: {}",
                                       i, slot.quantity, resourceTemplate->getMaxStackSize(), slot.resourceHandle.toString()));
          isValid = false;
          issueCount++;
        }
      }
    }
  }

  // Validate cache consistency
  if (!m_cacheNeedsRebuild) {
    for (const auto &[handle, cachedQuantity] : m_resourceQuantityCache) {
      const int actualQuantity = std::accumulate(
          m_slots.begin(), m_slots.end(), 0,
          [&handle](int sum, const InventorySlot &slot) {
            return slot.resourceHandle == handle ? sum + slot.quantity : sum;
          });

      if (actualQuantity != cachedQuantity) {
        INVENTORY_ERROR(
            std::format("validateInventoryIntegrity - Cache mismatch for handle {}: cached={}, actual={}",
                        handle.toString(), cachedQuantity, actualQuantity));
        isValid = false;
        issueCount++;
      }
    }
  }

  if (isValid) {
    INVENTORY_DEBUG("Inventory integrity validation passed");
  } else {
    INVENTORY_ERROR(std::format("Inventory integrity validation failed with {} issues", issueCount));
  }

  return isValid;
}

void InventoryComponent::reportInventoryState() const {
  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  INVENTORY_INFO("=== Inventory State Report ===");
  INVENTORY_INFO(std::format("Owner: {}", (m_owner ? "Present" : "No Owner")));
  INVENTORY_INFO(std::format("World ID: {}", m_worldId));
  INVENTORY_INFO(std::format("Max Slots: {}", m_maxSlots));
  INVENTORY_INFO(std::format("Used Slots: {}", getUsedSlots()));
  INVENTORY_INFO(std::format("World Resource Tracking: {}",
                 (m_trackWorldResources ? "enabled" : "disabled")));
  INVENTORY_INFO(std::format("Cache Needs Rebuild: {}",
                 (m_cacheNeedsRebuild ? "yes" : "no")));

  // Report non-empty slots
  size_t nonEmptySlots = 0;
  for (size_t i = 0; i < m_slots.size(); ++i) {
    const auto &slot = m_slots[i];
    if (!slot.isEmpty()) {
      auto resourceTemplate =
          ResourceTemplateManager::Instance().getResourceTemplate(
              slot.resourceHandle);
      std::string resourceName =
          resourceTemplate ? resourceTemplate->getName() : "Unknown Resource";

      INVENTORY_INFO(std::format("  Slot {}: {}x {} (handle: {})",
                                  i, slot.quantity, resourceName, slot.resourceHandle.toString()));
      nonEmptySlots++;
    }
  }

  if (nonEmptySlots == 0) {
    INVENTORY_INFO("  No items in inventory");
  }

  // Report cache state
  if (!m_cacheNeedsRebuild && !m_resourceQuantityCache.empty()) {
    INVENTORY_INFO(std::format("Cache entries: {}", m_resourceQuantityCache.size()));
    for (const auto &[handle, quantity] : m_resourceQuantityCache) {
      auto resourceTemplate =
          ResourceTemplateManager::Instance().getResourceTemplate(handle);
      std::string resourceName =
          resourceTemplate ? resourceTemplate->getName() : "Unknown Resource";
      INVENTORY_INFO(std::format("  Cache: {}x {}", quantity, resourceName));
    }
  }

  INVENTORY_INFO("=== End Inventory State Report ===");
}

size_t InventoryComponent::repairInventoryCorruption() {
  std::lock_guard<std::mutex> lock(m_inventoryMutex);

  size_t repairCount = 0;

  INVENTORY_INFO("Starting inventory corruption repair");

  // Repair invalid quantities
  for (auto &slot : m_slots) {
    if (!slot.isEmpty()) {
      // Fix negative quantities
      if (slot.quantity < 0) {
        INVENTORY_WARN(std::format("repairInventoryCorruption - Fixed negative quantity {} to 0 for handle: {}",
                                    slot.quantity, slot.resourceHandle.toString()));
        slot.quantity = 0;
        repairCount++;
      }

      // Fix excessive quantities
      if (!isValidQuantity(slot.quantity)) {
        INVENTORY_WARN(
            std::format("repairInventoryCorruption - Fixed excessive quantity {} to maximum for handle: {}",
                        slot.quantity, slot.resourceHandle.toString()));
        slot.quantity = MAX_SAFE_QUANTITY;
        repairCount++;
      }

      // Clear slots with invalid handles
      if (!slot.resourceHandle.isValid()) {
        INVENTORY_WARN(
            "repairInventoryCorruption - Cleared slot with invalid handle");
        slot.clear();
        repairCount++;
      }

      // Clear slots with zero quantity after repairs
      if (slot.quantity == 0) {
        slot.clear();
      }
    }
  }

  // Force cache rebuild after repairs
  if (repairCount > 0) {
    m_cacheNeedsRebuild = true;
    m_resourceQuantityCache.clear();

    INVENTORY_INFO(std::format("repairInventoryCorruption - Repaired {} corruption issues", repairCount));
  } else {
    INVENTORY_INFO("repairInventoryCorruption - No corruption found");
  }

  return repairCount;
}