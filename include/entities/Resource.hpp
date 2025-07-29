/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef RESOURCE_HPP
#define RESOURCE_HPP

#include "entities/Entity.hpp"
#include "utils/BinarySerializer.hpp"
#include "utils/ResourceHandle.hpp"
#include <memory>
#include <string>

// Forward declarations
class Resource;
using ResourcePtr = std::shared_ptr<Resource>;
using ResourceWeakPtr = std::weak_ptr<Resource>;

/**
 * @brief Resource category enumeration for organization and filtering
 */
enum class ResourceCategory : uint8_t {
  Item = 0,         // Equipment, consumables, quest items
  Material = 1,     // Crafting components, raw resources
  Currency = 2,     // Gold, gems, faction tokens
  GameResource = 3, // Energy, mana, building materials, ammunition
  COUNT = 4
};

/**
 * @brief Resource type enumeration for specific resource identification
 */
enum class ResourceType : uint8_t {
  // Items
  Equipment = 0,
  Consumable = 1,
  QuestItem = 2,

  // Materials
  CraftingComponent = 10,
  RawResource = 11,

  // Currency
  Gold = 20,
  Gem = 21,
  FactionToken = 22,

  // Game Resources
  Energy = 30,
  Mana = 31,
  BuildingMaterial = 32,
  Ammunition = 33,

  COUNT = 34
};

/**
 * @brief Base resource entity class that all resources inherit from
 *
 * Resources are immutable templates that define the properties of items,
 * materials, currency, and game resources. Individual instances are managed by
 * InventoryComponent.
 */
class Resource : public Entity {
public:
  Resource(HammerEngine::ResourceHandle handle, const std::string &name,
           ResourceCategory category, ResourceType type);
  virtual ~Resource() override = default;

  // Entity interface implementation
  void update(float deltaTime) override;
  void render() override;
  void clean() override;

  // Resource properties (immutable)
  HammerEngine::ResourceHandle getHandle() const { return m_handle; }
  const std::string &getName() const { return m_name; }
  const std::string &getDescription() const { return m_description; }
  ResourceCategory getCategory() const { return m_category; }
  ResourceType getType() const { return m_type; }
  float getValue() const { return m_value; }
  int getMaxStackSize() const { return m_maxStackSize; }
  bool isStackable() const { return m_isStackable; }
  bool isConsumable() const { return m_isConsumable; }
  const std::string &getIconTextureId() const { return m_iconTextureId; }

  // Property setters (for initialization only)
  void setDescription(const std::string &description) {
    m_description = description;
  }
  void setValue(float value) { m_value = value; }
  void setMaxStackSize(int maxStack) {
    m_maxStackSize = maxStack;
    m_isStackable = (maxStack > 1);
  }
  void setConsumable(bool consumable) { m_isConsumable = consumable; }
  void setIconTextureId(const std::string &textureId) {
    m_iconTextureId = textureId;
  }

  // Factory method for proper shared_ptr creation
  template <typename T, typename... Args>
  static std::shared_ptr<T> create(Args &&...args) {
    static_assert(std::is_base_of_v<Resource, T>,
                  "T must derive from Resource");
    return std::make_shared<T>(std::forward<Args>(args)...);
  }

  // TODO: Implement proper ISerializable interface later
  // bool serialize(std::ostream &stream) const override;
  // bool deserialize(std::istream &stream) override;

  // Utility functions
  static std::string categoryToString(ResourceCategory category);
  static std::string typeToString(ResourceType type);
  static ResourceCategory stringToCategory(const std::string &categoryStr);
  static ResourceType stringToType(const std::string &typeStr);

protected:
  HammerEngine::ResourceHandle m_handle; // Unique handle identifier
  std::string m_name;                    // Display name
  std::string m_description{""};         // Description text
  ResourceCategory m_category;           // Resource category
  ResourceType m_type;                   // Specific resource type
  float m_value{0.0f};                   // Base value/cost
  int m_maxStackSize{1};                 // Maximum stack size
  bool m_isStackable{false};             // Can be stacked
  bool m_isConsumable{false};            // Can be consumed/used
  std::string m_iconTextureId{""};       // Texture ID for icon
};

#endif // RESOURCE_HPP