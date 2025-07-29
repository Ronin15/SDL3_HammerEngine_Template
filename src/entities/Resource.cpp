/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/Resource.hpp"
#include "core/Logger.hpp"
#include <unordered_map>

Resource::Resource(HammerEngine::ResourceHandle handle, const std::string &id,
                   const std::string &name, ResourceCategory category,
                   ResourceType type)
    : m_handle(handle), m_id(id), m_name(name), m_category(category),
      m_type(type) {

  // Initialize base Entity properties (resources don't render by default)
  m_position = Vector2D(0, 0);
  m_velocity = Vector2D(0, 0);
  m_acceleration = Vector2D(0, 0);
  m_width = 0;
  m_height = 0;
  m_textureID = "";
  m_currentFrame = 0;
  m_currentRow = 0;
  m_numFrames = 0;
  m_animSpeed = 0;

  // Set default properties based on category
  switch (category) {
  case ResourceCategory::Item:
    m_maxStackSize = 1;
    m_isStackable = false;
    break;
  case ResourceCategory::Material:
    m_maxStackSize = 999;
    m_isStackable = true;
    break;
  case ResourceCategory::Currency:
    m_maxStackSize = 999999;
    m_isStackable = true;
    break;
  case ResourceCategory::GameResource:
    m_maxStackSize = 9999;
    m_isStackable = true;
    break;
  default:
    m_maxStackSize = 1;
    m_isStackable = false;
    break;
  }

  RESOURCE_INFO("Created resource: " + m_name +
                " (Handle: " + m_handle.toString() + ")");
}

void Resource::update(float) {
  // Resources are templates - they don't update themselves
  // Individual instances are managed by InventoryComponent
}

void Resource::render() {
  // Resources don't render themselves (they're templates)
  // Individual instances are rendered by UI systems using the icon texture
}

void Resource::clean() {
  // Clean up any resource-specific data
  m_description.clear();
  m_iconTextureId.clear();
}

std::string Resource::categoryToString(ResourceCategory category) {
  static const std::unordered_map<ResourceCategory, std::string> categoryMap = {
      {ResourceCategory::Item, "Item"},
      {ResourceCategory::Material, "Material"},
      {ResourceCategory::Currency, "Currency"},
      {ResourceCategory::GameResource, "GameResource"}};

  auto it = categoryMap.find(category);
  return (it != categoryMap.end()) ? it->second : "Unknown";
}

std::string Resource::typeToString(ResourceType type) {
  static const std::unordered_map<ResourceType, std::string> typeMap = {
      // Items
      {ResourceType::Equipment, "Equipment"},
      {ResourceType::Consumable, "Consumable"},
      {ResourceType::QuestItem, "QuestItem"},

      // Materials
      {ResourceType::CraftingComponent, "CraftingComponent"},
      {ResourceType::RawResource, "RawResource"},

      // Currency
      {ResourceType::Gold, "Gold"},
      {ResourceType::Gem, "Gem"},
      {ResourceType::FactionToken, "FactionToken"},

      // Game Resources
      {ResourceType::Energy, "Energy"},
      {ResourceType::Mana, "Mana"},
      {ResourceType::BuildingMaterial, "BuildingMaterial"},
      {ResourceType::Ammunition, "Ammunition"}};

  auto it = typeMap.find(type);
  return (it != typeMap.end()) ? it->second : "Unknown";
}

ResourceCategory Resource::stringToCategory(const std::string &categoryStr) {
  static const std::unordered_map<std::string, ResourceCategory> categoryMap = {
      {"Item", ResourceCategory::Item},
      {"Material", ResourceCategory::Material},
      {"Currency", ResourceCategory::Currency},
      {"GameResource", ResourceCategory::GameResource}};

  auto it = categoryMap.find(categoryStr);
  return (it != categoryMap.end()) ? it->second : ResourceCategory::Item;
}

ResourceType Resource::stringToType(const std::string &typeStr) {
  static const std::unordered_map<std::string, ResourceType> typeMap = {
      // Items
      {"Equipment", ResourceType::Equipment},
      {"Consumable", ResourceType::Consumable},
      {"QuestItem", ResourceType::QuestItem},

      // Materials
      {"CraftingComponent", ResourceType::CraftingComponent},
      {"RawResource", ResourceType::RawResource},

      // Currency
      {"Gold", ResourceType::Gold},
      {"Gem", ResourceType::Gem},
      {"FactionToken", ResourceType::FactionToken},

      // Game Resources
      {"Energy", ResourceType::Energy},
      {"Mana", ResourceType::Mana},
      {"BuildingMaterial", ResourceType::BuildingMaterial},
      {"Ammunition", ResourceType::Ammunition}};

  auto it = typeMap.find(typeStr);
  return (it != typeMap.end()) ? it->second : ResourceType::Equipment;
}