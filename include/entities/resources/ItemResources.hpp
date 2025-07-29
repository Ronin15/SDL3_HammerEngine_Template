/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef ITEM_RESOURCES_HPP
#define ITEM_RESOURCES_HPP

#include "entities/Resource.hpp"

/**
 * @brief Base class for all item resources (equipment, consumables, quest
 * items)
 */
class Item : public Resource {
public:
  Item(HammerEngine::ResourceHandle handle, const std::string &name,
       ResourceType type);
  virtual ~Item() = default;

  // Item-specific properties
  int getDurability() const { return m_durability; }
  int getMaxDurability() const { return m_maxDurability; }
  void setDurability(int durability, int maxDurability);

  // TODO: Implement proper serialization later
  // bool serialize(std::ostream &stream) const override;
  // bool deserialize(std::istream &stream) override;

protected:
  int m_durability{100};    // Current durability
  int m_maxDurability{100}; // Maximum durability
};

/**
 * @brief Equipment items (weapons, armor, accessories)
 */
class Equipment : public Item {
public:
  enum class EquipmentSlot : uint8_t {
    Weapon = 0,
    Helmet = 1,
    Chest = 2,
    Legs = 3,
    Boots = 4,
    Gloves = 5,
    Ring = 6,
    Necklace = 7,
    COUNT = 8
  };

  Equipment(HammerEngine::ResourceHandle handle, const std::string &name,
            EquipmentSlot slot);
  virtual ~Equipment() = default;

  EquipmentSlot getEquipmentSlot() const { return m_equipmentSlot; }
  int getAttackBonus() const { return m_attackBonus; }
  int getDefenseBonus() const { return m_defenseBonus; }
  int getSpeedBonus() const { return m_speedBonus; }

  void setAttackBonus(int bonus) { m_attackBonus = bonus; }
  void setDefenseBonus(int bonus) { m_defenseBonus = bonus; }
  void setSpeedBonus(int bonus) { m_speedBonus = bonus; }

  // TODO: Implement proper serialization later
  // bool serialize(std::ostream &stream) const override;
  // bool deserialize(std::istream &stream) override;

  static std::string equipmentSlotToString(EquipmentSlot slot);

private:
  EquipmentSlot m_equipmentSlot;
  int m_attackBonus{0};
  int m_defenseBonus{0};
  int m_speedBonus{0};
};

/**
 * @brief Consumable items (potions, food, scrolls)
 */
class Consumable : public Item {
public:
  enum class ConsumableEffect : uint8_t {
    HealHP = 0,
    RestoreMP = 1,
    BoostAttack = 2,
    BoostDefense = 3,
    BoostSpeed = 4,
    Teleport = 5,
    COUNT = 6
  };

  Consumable(HammerEngine::ResourceHandle handle, const std::string &name);
  virtual ~Consumable() = default;

  ConsumableEffect getEffect() const { return m_effect; }
  int getEffectPower() const { return m_effectPower; }
  int getEffectDuration() const { return m_effectDuration; }

  void setEffect(ConsumableEffect effect) { m_effect = effect; }
  void setEffectPower(int power) { m_effectPower = power; }
  void setEffectDuration(int duration) { m_effectDuration = duration; }

  // TODO: Implement proper serialization later
  // bool serialize(std::ostream &stream) const override;
  // bool deserialize(std::istream &stream) override;

  static std::string consumableEffectToString(ConsumableEffect effect);

private:
  ConsumableEffect m_effect{ConsumableEffect::HealHP};
  int m_effectPower{10};   // Strength of the effect
  int m_effectDuration{0}; // Duration in seconds (0 = instant)
};

/**
 * @brief Quest items (keys, documents, special objects)
 */
class QuestItem : public Item {
public:
  QuestItem(HammerEngine::ResourceHandle handle, const std::string &name,
            const std::string &questId = "");
  virtual ~QuestItem() = default;

  const std::string &getQuestId() const { return m_questId; }
  bool isQuestSpecific() const { return !m_questId.empty(); }

  void setQuestId(const std::string &questId) { m_questId = questId; }

  // TODO: Implement proper serialization later
  // bool serialize(std::ostream &stream) const override;
  // bool deserialize(std::istream &stream) override;

private:
  std::string m_questId{""}; // Associated quest ID (empty = general quest item)
};

#endif // ITEM_RESOURCES_HPP