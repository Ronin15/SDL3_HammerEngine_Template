/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/resources/CurrencyAndGameResources.hpp"
#include "core/Logger.hpp"
#include <unordered_map>

// Currency base class implementation
Currency::Currency(const std::string &id, const std::string &name,
                   ResourceType type)
    : Resource(id, name, ResourceCategory::Currency, type) {
  // Currencies are highly stackable
  setMaxStackSize(9999999);
  setConsumable(false);
}

// Gold currency implementation
Gold::Gold(const std::string &id, const std::string &name)
    : Currency(id, name, ResourceType::Gold) {
  setValue(1.0f);
  setExchangeRate(1.0f); // Base currency
  setDescription("Standard gold currency");
  setIconTextureId("currency_gold");
}

// Gem currency implementation
Gem::Gem(const std::string &id, const std::string &name, GemType gemType)
    : Currency(id, name, ResourceType::Gem), m_gemType(gemType) {
  // Set default values based on gem type
  switch (gemType) {
  case GemType::Ruby:
    setValue(10.0f);
    setExchangeRate(10.0f);
    setClarity(5);
    break;
  case GemType::Emerald:
    setValue(12.0f);
    setExchangeRate(12.0f);
    setClarity(6);
    break;
  case GemType::Sapphire:
    setValue(15.0f);
    setExchangeRate(15.0f);
    setClarity(7);
    break;
  case GemType::Diamond:
    setValue(25.0f);
    setExchangeRate(25.0f);
    setClarity(8);
    break;
  case GemType::COUNT:
    break;
  }
  setDescription("Precious gem: " + gemTypeToString(gemType));
  setIconTextureId("gem_" + gemTypeToString(gemType));
}

std::string Gem::gemTypeToString(GemType type) {
  static const std::unordered_map<GemType, std::string> typeMap = {
      {GemType::Ruby, "Ruby"},
      {GemType::Emerald, "Emerald"},
      {GemType::Sapphire, "Sapphire"},
      {GemType::Diamond, "Diamond"}};

  auto it = typeMap.find(type);
  return (it != typeMap.end()) ? it->second : "Unknown";
}

// FactionToken implementation
FactionToken::FactionToken(const std::string &id, const std::string &name,
                           const std::string &factionId)
    : Currency(id, name, ResourceType::FactionToken), m_factionId(factionId) {
  setValue(1.0f);
  setExchangeRate(0.0f); // Cannot be exchanged for gold
  setDescription("Faction token for " + factionId);
  setIconTextureId("token_" + factionId);
}

// GameResource base class implementation
GameResource::GameResource(const std::string &id, const std::string &name,
                           ResourceType type)
    : Resource(id, name, ResourceCategory::GameResource, type) {
  // Game resources are highly stackable
  setMaxStackSize(99999);
  setConsumable(true); // Can be consumed/used
}

// Energy implementation
Energy::Energy(const std::string &id, const std::string &name)
    : GameResource(id, name, ResourceType::Energy) {
  setValue(0.1f);
  setRegenerationRate(1.0f); // 1 energy per second
  setDescription("Energy for actions and abilities");
  setIconTextureId("energy");
}

// Mana implementation
Mana::Mana(const std::string &id, const std::string &name, ManaType manaType)
    : GameResource(id, name, ResourceType::Mana), m_manaType(manaType) {
  setValue(0.2f);
  setRegenerationRate(0.5f); // 0.5 mana per second
  setDescription("Magical energy: " + manaTypeToString(manaType));
  setIconTextureId("mana_" + manaTypeToString(manaType));
}

std::string Mana::manaTypeToString(ManaType type) {
  static const std::unordered_map<ManaType, std::string> typeMap = {
      {ManaType::Arcane, "Arcane"},
      {ManaType::Divine, "Divine"},
      {ManaType::Nature, "Nature"},
      {ManaType::Dark, "Dark"}};

  auto it = typeMap.find(type);
  return (it != typeMap.end()) ? it->second : "Unknown";
}

// BuildingMaterial implementation
BuildingMaterial::BuildingMaterial(const std::string &id,
                                   const std::string &name,
                                   MaterialType materialType)
    : GameResource(id, name, ResourceType::BuildingMaterial),
      m_materialType(materialType) {
  // Set default values based on material type
  switch (materialType) {
  case MaterialType::Wood:
    setValue(2.0f);
    setDurability(50);
    break;
  case MaterialType::Stone:
    setValue(5.0f);
    setDurability(100);
    break;
  case MaterialType::Metal:
    setValue(10.0f);
    setDurability(150);
    break;
  case MaterialType::Crystal:
    setValue(20.0f);
    setDurability(200);
    break;
  case MaterialType::COUNT:
    break;
  }
  setDescription("Building material: " + materialTypeToString(materialType));
  setIconTextureId("building_" + materialTypeToString(materialType));
}

std::string BuildingMaterial::materialTypeToString(MaterialType type) {
  static const std::unordered_map<MaterialType, std::string> typeMap = {
      {MaterialType::Wood, "Wood"},
      {MaterialType::Stone, "Stone"},
      {MaterialType::Metal, "Metal"},
      {MaterialType::Crystal, "Crystal"}};

  auto it = typeMap.find(type);
  return (it != typeMap.end()) ? it->second : "Unknown";
}

// Ammunition implementation
Ammunition::Ammunition(const std::string &id, const std::string &name,
                       AmmoType ammoType)
    : GameResource(id, name, ResourceType::Ammunition), m_ammoType(ammoType) {
  // Set default values based on ammo type
  switch (ammoType) {
  case AmmoType::Arrow:
    setValue(1.0f);
    setDamage(10);
    setMaxStackSize(200);
    break;
  case AmmoType::Bolt:
    setValue(1.5f);
    setDamage(12);
    setMaxStackSize(150);
    break;
  case AmmoType::Bullet:
    setValue(2.0f);
    setDamage(15);
    setMaxStackSize(100);
    break;
  case AmmoType::ThrowingKnife:
    setValue(3.0f);
    setDamage(8);
    setMaxStackSize(50);
    break;
  case AmmoType::MagicMissile:
    setValue(5.0f);
    setDamage(20);
    setMaxStackSize(25);
    break;
  case AmmoType::COUNT:
    break;
  }
  setDescription("Ammunition: " + ammoTypeToString(ammoType));
  setIconTextureId("ammo_" + ammoTypeToString(ammoType));
}

std::string Ammunition::ammoTypeToString(AmmoType type) {
  static const std::unordered_map<AmmoType, std::string> typeMap = {
      {AmmoType::Arrow, "Arrow"},
      {AmmoType::Bolt, "Bolt"},
      {AmmoType::Bullet, "Bullet"},
      {AmmoType::ThrowingKnife, "ThrowingKnife"},
      {AmmoType::MagicMissile, "MagicMissile"}};

  auto it = typeMap.find(type);
  return (it != typeMap.end()) ? it->second : "Unknown";
}