/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/resources/ItemResources.hpp"
#include <unordered_map>

// Item base class implementation
Item::Item(HammerEngine::ResourceHandle handle, const std::string &name,
           ResourceType type)
    : Resource(handle, name, ResourceCategory::Item, type) {
  // Items are moderately stackable
  setMaxStackSize(50);
  setConsumable(false);
}

void Item::setDurability(int durability, int maxDurability) {
  m_durability = durability;
  m_maxDurability = maxDurability;
}

// Equipment implementation
Equipment::Equipment(HammerEngine::ResourceHandle handle,
                     const std::string &name, EquipmentSlot slot)
    : Item(handle, name, ResourceType::Equipment), m_equipmentSlot(slot) {
  // Equipment is not stackable
  setMaxStackSize(1);
  setConsumable(false);
}

std::string Equipment::equipmentSlotToString(EquipmentSlot slot) {
  static const std::unordered_map<EquipmentSlot, std::string> slotMap = {
      {EquipmentSlot::Weapon, "Weapon"}, {EquipmentSlot::Helmet, "Helmet"},
      {EquipmentSlot::Chest, "Chest"},   {EquipmentSlot::Legs, "Legs"},
      {EquipmentSlot::Boots, "Boots"},   {EquipmentSlot::Gloves, "Gloves"},
      {EquipmentSlot::Ring, "Ring"},     {EquipmentSlot::Necklace, "Necklace"}};

  auto it = slotMap.find(slot);
  return (it != slotMap.end()) ? it->second : "Unknown";
}

// Consumable implementation
Consumable::Consumable(HammerEngine::ResourceHandle handle,
                       const std::string &name)
    : Item(handle, name, ResourceType::Consumable) {
  // Consumables are highly stackable
  setMaxStackSize(100);
  setConsumable(true);
}

std::string Consumable::consumableEffectToString(ConsumableEffect effect) {
  static const std::unordered_map<ConsumableEffect, std::string> effectMap = {
      {ConsumableEffect::HealHP, "Heal HP"},
      {ConsumableEffect::RestoreMP, "Restore MP"},
      {ConsumableEffect::BoostAttack, "Boost Attack"},
      {ConsumableEffect::BoostDefense, "Boost Defense"},
      {ConsumableEffect::BoostSpeed, "Boost Speed"},
      {ConsumableEffect::Teleport, "Teleport"}};

  auto it = effectMap.find(effect);
  return (it != effectMap.end()) ? it->second : "Unknown";
}

// QuestItem implementation
QuestItem::QuestItem(HammerEngine::ResourceHandle handle,
                     const std::string &name, const std::string &questId)
    : Item(handle, name, ResourceType::QuestItem), m_questId(questId) {
  // Quest items are not stackable and not consumable
  setMaxStackSize(1);
  setConsumable(false);
}