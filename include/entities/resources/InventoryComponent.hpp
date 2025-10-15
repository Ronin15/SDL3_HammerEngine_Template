/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef INVENTORY_COMPONENT_HPP
#define INVENTORY_COMPONENT_HPP

#include "entities/Resource.hpp"
#include "utils/ResourceHandle.hpp"
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
class Entity;

/**
 * @brief Inventory slot data structure
 */
struct InventorySlot {
  HammerEngine::ResourceHandle resourceHandle{
      HammerEngine::INVALID_RESOURCE_HANDLE};
  int quantity{0};

  InventorySlot() = default;
  InventorySlot(HammerEngine::ResourceHandle handle, int qty)
      : resourceHandle(handle), quantity(qty) {}

  bool isEmpty() const { return !resourceHandle.isValid() || quantity <= 0; }
  void clear() {
    resourceHandle = HammerEngine::INVALID_RESOURCE_HANDLE;
    quantity = 0;
  }
};

/**
 * @brief Component for managing entity inventories
 *
 * This component handles resource storage, quantity tracking, and inventory
 * operations for any entity that needs to store resources (Player, NPC,
 * containers, etc.)
 */
class InventoryComponent {
public:
  using ResourceChangeCallback =
      std::function<void(HammerEngine::ResourceHandle, int, int)>;

  explicit InventoryComponent(Entity *owner = nullptr, size_t maxSlots = 50,
                              const std::string &worldId = "default");
  virtual ~InventoryComponent() = default;

  // Basic inventory operations (ResourceHandle-based)
  bool addResource(HammerEngine::ResourceHandle handle, int quantity);
  bool removeResource(HammerEngine::ResourceHandle handle, int quantity);
  int getResourceQuantity(HammerEngine::ResourceHandle handle) const;
  bool hasResource(HammerEngine::ResourceHandle handle,
                   int minimumQuantity = 1) const;

  // Inventory management
  void clearInventory();
  size_t getUsedSlots() const;
  size_t getMaxSlots() const { return m_maxSlots; }
  size_t getAvailableSlots() const;
  bool isFull() const;
  bool isEmpty() const;

  // Safe quantity limits and validation
  static constexpr int MAX_SAFE_QUANTITY = 1000000; // 1 million max per slot
  static constexpr int MIN_SAFE_QUANTITY = 0;
  bool isValidQuantity(int quantity) const;
  bool wouldOverflow(int currentQuantity, int addQuantity) const;
  bool wouldUnderflow(int currentQuantity, int removeQuantity) const;

  // Diagnostic and error recovery
  bool validateInventoryIntegrity() const;
  void reportInventoryState() const;
  size_t repairInventoryCorruption();

  // Category-based queries
  std::vector<InventorySlot>
  getResourcesByCategory(ResourceCategory category) const;
  std::unordered_map<HammerEngine::ResourceHandle, int> getAllResources() const;
  std::vector<HammerEngine::ResourceHandle> getResourceHandles() const;

  // Slot-based operations (for grid-based inventories)
  const InventorySlot &getSlot(size_t slotIndex) const;
  bool setSlot(size_t slotIndex, HammerEngine::ResourceHandle handle,
               int quantity);
  bool swapSlots(size_t slotA, size_t slotB);
  bool moveResource(size_t fromSlot, size_t toSlot, int quantity = -1);

  // Transfer operations
  bool transferTo(InventoryComponent &target,
                  HammerEngine::ResourceHandle handle, int quantity);
  bool transferSlotTo(InventoryComponent &target, size_t slotIndex,
                      int quantity = -1);

  // Event handling
  void setResourceChangeCallback(ResourceChangeCallback callback) {
    m_onResourceChanged = callback;
  }
  void clearResourceChangeCallback() { m_onResourceChanged = nullptr; }

  /**
   * @brief Thread-safety documentation for resource change callbacks
   *
   * IMPORTANT: Resource change callbacks are invoked OUTSIDE of inventory locks
   * to prevent deadlocks. However, this means:
   *
   * 1. Callbacks may be called from multiple threads simultaneously if the
   *    inventory is accessed concurrently
   * 2. Callbacks should NOT call back into this inventory's methods as this
   *    could cause recursive locking or inconsistent state
   * 3. If callbacks need to access shared state, they must provide their own
   *    thread synchronization
   * 4. Callbacks should be lightweight and non-blocking to avoid performance
   * issues
   * 5. Exception safety: callbacks should not throw exceptions as they are not
   *    caught by the inventory component
   *
   * Best practices:
   * - Use callbacks for notifications, logging, or queuing work for other
   * threads
   * - Avoid long-running operations in callbacks
   * - Consider using event queues instead of direct callbacks for complex
   * scenarios
   */

  // Serialization support
  // TODO: Implement proper serialization later
  // bool serialize(std::ostream &stream) const;
  // bool deserialize(std::istream &stream);
  // Utility functions
  float getTotalValue() const;
  float getTotalWeight() const;
  bool canAddResource(HammerEngine::ResourceHandle handle, int quantity) const;
  int getStackableQuantity(HammerEngine::ResourceHandle handle) const;

  // Sorting and organization
  void sortByCategory();
  void sortByValue();
  void sortByName();
  void compactInventory(); // Remove empty slots and stack items

  // Owner management
  Entity *getOwner() const { return m_owner; }
  void setOwner(Entity *owner) { m_owner = owner; }

  // World management
  const std::string &getWorldId() const { return m_worldId; }
  void setWorldId(const std::string &worldId) { m_worldId = worldId; }

  // WorldResourceManager integration
  void setWorldResourceTracking(bool enabled) {
    m_trackWorldResources = enabled;
  }
  bool isWorldResourceTrackingEnabled() const { return m_trackWorldResources; }

protected:
  Entity *m_owner;                            // Entity that owns this inventory
  std::vector<InventorySlot> m_slots;         // Inventory slots
  size_t m_maxSlots;                          // Maximum number of slots
  ResourceChangeCallback m_onResourceChanged; // Callback for resource changes
  mutable std::mutex m_inventoryMutex;        // Thread safety
  std::string m_worldId;                      // World ID for resource tracking
  bool m_trackWorldResources;                 // Whether to track resources in
                                              // WorldResourceManager

  // Performance optimization: cache resource quantities for O(1) lookups
  mutable std::unordered_map<HammerEngine::ResourceHandle, int>
      m_resourceQuantityCache;
  mutable bool m_cacheNeedsRebuild{false};

  // Helper methods
  int findSlotWithResource(HammerEngine::ResourceHandle handle) const;
  int findEmptySlot() const;
  bool canStackInSlot(size_t slotIndex,
                      HammerEngine::ResourceHandle handle) const;
  void notifyResourceChange(HammerEngine::ResourceHandle handle,
                            int oldQuantity, int newQuantity);
  void notifyResourceChangeSafe(HammerEngine::ResourceHandle handle,
                                int oldQuantity, int newQuantity);
  void validateSlotIndex(size_t slotIndex) const;
  int getResourceQuantityUnlocked(HammerEngine::ResourceHandle handle) const;

  // Resource lookup helper (removed - use handles only)
  // HammerEngine::ResourceHandle
  // findResourceByName(const std::string &name) const;

  // Cache management for performance optimization
  void updateQuantityCache(HammerEngine::ResourceHandle handle,
                           int quantityChange);
  void rebuildQuantityCache() const;

  // WorldResourceManager integration helpers
  void updateWorldResourceManager(HammerEngine::ResourceHandle handle,
                                  int quantityChange) const;
};

#endif // INVENTORY_COMPONENT_HPP
