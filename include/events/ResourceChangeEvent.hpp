/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef RESOURCE_CHANGE_EVENT_HPP
#define RESOURCE_CHANGE_EVENT_HPP

#include "entities/Entity.hpp"
#include "events/Event.hpp"
#include "utils/ResourceHandle.hpp"
#include <memory>
#include <string>

/**
 * @brief Event fired when a resource quantity changes in an inventory
 *
 * This event is triggered whenever resources are added, removed, or modified
 * in any InventoryComponent. It allows systems to react to inventory changes
 * such as updating UI displays, triggering achievements, or logging
 * transactions.
 */
class ResourceChangeEvent : public Event {
public:
  /**
   * @brief Constructs a resource change event
   * @param owner Entity that owns the inventory where the change occurred
   * @param resourceHandle Handle of the resource that changed
   * @param oldQuantity Previous quantity of the resource
   * @param newQuantity New quantity of the resource
   * @param changeReason Optional reason for the change (e.g., "crafted",
   * "consumed", "traded")
   */
  ResourceChangeEvent(EntityPtr owner,
                      HammerEngine::ResourceHandle resourceHandle,
                      int oldQuantity, int newQuantity,
                      const std::string &changeReason = "");

  virtual ~ResourceChangeEvent() override = default;

  // Event interface implementation
  void update() override {}
  void execute() override {}
  void reset() override {}
  void clean() override {}
  std::string getName() const override { return "ResourceChange"; }
  bool checkConditions() override { return true; }
  std::string getType() const override { return EVENT_TYPE; }
  std::string getTypeName() const override { return "ResourceChangeEvent"; }
  static const std::string EVENT_TYPE;

  // Resource change data
  EntityWeakPtr getOwner() const { return m_owner; }
  HammerEngine::ResourceHandle getResourceHandle() const {
    return m_resourceHandle;
  }
  int getOldQuantity() const { return m_oldQuantity; }
  int getNewQuantity() const { return m_newQuantity; }
  int getQuantityChange() const { return m_newQuantity - m_oldQuantity; }
  const std::string &getChangeReason() const { return m_changeReason; }

  // Convenience methods
  bool isIncrease() const { return m_newQuantity > m_oldQuantity; }
  bool isDecrease() const { return m_newQuantity < m_oldQuantity; }
  bool isResourceAdded() const {
    return m_oldQuantity == 0 && m_newQuantity > 0;
  }
  bool isResourceRemoved() const {
    return m_oldQuantity > 0 && m_newQuantity == 0;
  }

private:
  EntityWeakPtr m_owner; // Entity that owns the inventory
  HammerEngine::ResourceHandle
      m_resourceHandle;       // Handle of the resource that changed
  int m_oldQuantity;          // Previous quantity
  int m_newQuantity;          // New quantity
  std::string m_changeReason; // Reason for the change
};

#endif // RESOURCE_CHANGE_EVENT_HPP