/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef CURRENCY_AND_GAME_RESOURCES_HPP
#define CURRENCY_AND_GAME_RESOURCES_HPP

#include "entities/Resource.hpp"

/**
 * @brief Base class for currency resources (gold, gems, faction tokens)
 */
class Currency : public Resource {
public:
  Currency(HammerEngine::ResourceHandle handle, const std::string &id,
           const std::string &name, ResourceType type);
  ~Currency() override = default;

  // Currency-specific properties
  float getExchangeRate() const { return m_exchangeRate; }
  void setExchangeRate(float rate) { m_exchangeRate = rate; }

  // TODO: Implement proper serialization later
  // bool serialize(std::ostream &stream) const override;
  // bool deserialize(std::istream &stream) override;

protected:
  float m_exchangeRate{1.0f}; // Exchange rate to base currency (gold)
};

/**
 * @brief Gold currency (base currency)
 */
class Gold : public Currency {
public:
  Gold(HammerEngine::ResourceHandle handle, const std::string &id,
       const std::string &name);
  ~Gold() override = default;
};

/**
 * @brief Gem currency (precious stones)
 */
class Gem : public Currency {
public:
  enum class GemType : uint8_t {
    Ruby = 0,
    Emerald = 1,
    Sapphire = 2,
    Diamond = 3,
    COUNT = 4
  };

  Gem(HammerEngine::ResourceHandle handle, const std::string &id,
      const std::string &name, GemType gemType);
  ~Gem() override = default;

  GemType getGemType() const { return m_gemType; }
  int getClarity() const { return m_clarity; }
  void setClarity(int clarity) { m_clarity = clarity; }

  // TODO: Implement proper serialization later
  // bool serialize(std::ostream &stream) const override;
  // bool deserialize(std::istream &stream) override;

  static std::string gemTypeToString(GemType type);

private:
  GemType m_gemType;
  int m_clarity{5}; // Clarity rating 1-10 (affects value)
};

/**
 * @brief Faction token currency (reputation-based currency)
 */
class FactionToken : public Currency {
public:
  FactionToken(HammerEngine::ResourceHandle handle, const std::string &id,
               const std::string &name, const std::string &factionId);
  ~FactionToken() override = default;

  const std::string &getFactionId() const { return m_factionId; }
  int getReputation() const { return m_reputation; }
  void setReputation(int reputation) { m_reputation = reputation; }

  // TODO: Implement proper serialization later
  // bool serialize(std::ostream &stream) const override;
  // bool deserialize(std::istream &stream) override;

private:
  std::string m_factionId;
  int m_reputation{0}; // Required reputation to earn this token
};

/**
 * @brief Base class for game resources (energy, mana, building materials,
 * ammunition)
 */
class GameResource : public Resource {
public:
  GameResource(HammerEngine::ResourceHandle handle, const std::string &id,
               const std::string &name, ResourceType type);
  ~GameResource() override = default;

  // Game resource properties
  float getRegenerationRate() const { return m_regenerationRate; }
  void setRegenerationRate(float rate) { m_regenerationRate = rate; }

  // TODO: Implement proper serialization later
  // bool serialize(std::ostream &stream) const override;
  // bool deserialize(std::istream &stream) override;

protected:
  float m_regenerationRate{0.0f}; // Rate of automatic regeneration per second
};

/**
 * @brief Energy resource (stamina, action points, etc.)
 */
class Energy : public GameResource {
public:
  Energy(HammerEngine::ResourceHandle handle, const std::string &id,
         const std::string &name);
  ~Energy() override = default;

  int getMaxEnergy() const { return m_maxEnergy; }
  void setMaxEnergy(int maxEnergy) { m_maxEnergy = maxEnergy; }

  // TODO: Implement proper serialization later
  // bool serialize(std::ostream &stream) const override;
  // bool deserialize(std::istream &stream) override;

private:
  int m_maxEnergy{100}; // Maximum energy capacity
};

/**
 * @brief Mana resource (magical energy)
 */
class Mana : public GameResource {
public:
  enum class ManaType : uint8_t {
    Arcane = 0,
    Divine = 1,
    Nature = 2,
    Dark = 3,
    COUNT = 4
  };

  Mana(HammerEngine::ResourceHandle handle, const std::string &id,
       const std::string &name, ManaType manaType = ManaType::Arcane);
  ~Mana() override = default;

  ManaType getManaType() const { return m_manaType; }
  int getMaxMana() const { return m_maxMana; }
  void setMaxMana(int maxMana) { m_maxMana = maxMana; }

  // TODO: Implement proper serialization later
  // bool serialize(std::ostream &stream) const override;
  // bool deserialize(std::istream &stream) override;

  static std::string manaTypeToString(ManaType type);

private:
  ManaType m_manaType;
  int m_maxMana{100}; // Maximum mana capacity
};

/**
 * @brief Building material resource (wood, stone, metal for construction)
 */
class BuildingMaterial : public GameResource {
public:
  enum class MaterialType : uint8_t {
    Wood = 0,
    Stone = 1,
    Metal = 2,
    Crystal = 3,
    COUNT = 4
  };

  BuildingMaterial(HammerEngine::ResourceHandle handle, const std::string &id,
                   const std::string &name, MaterialType materialType);
  ~BuildingMaterial() override = default;

  MaterialType getMaterialType() const { return m_materialType; }
  int getDurability() const { return m_durability; }
  void setDurability(int durability) { m_durability = durability; }

  // TODO: Implement proper serialization later
  // bool serialize(std::ostream &stream) const override;
  // bool deserialize(std::istream &stream) override;

  static std::string materialTypeToString(MaterialType type);

private:
  MaterialType m_materialType;
  int m_durability{100}; // Durability of structures built with this material
};

/**
 * @brief Ammunition resource (arrows, bullets, throwing weapons)
 */
class Ammunition : public GameResource {
public:
  enum class AmmoType : uint8_t {
    Arrow = 0,
    Bolt = 1,
    Bullet = 2,
    ThrowingKnife = 3,
    MagicMissile = 4,
    COUNT = 5
  };

  Ammunition(HammerEngine::ResourceHandle handle, const std::string &id,
             const std::string &name, AmmoType ammoType);
  ~Ammunition() override = default;

  AmmoType getAmmoType() const { return m_ammoType; }
  int getDamage() const { return m_damage; }
  void setDamage(int damage) { m_damage = damage; }

  // TODO: Implement proper serialization later
  // bool serialize(std::ostream &stream) const override;
  // bool deserialize(std::istream &stream) override;

  static std::string ammoTypeToString(AmmoType type);

private:
  AmmoType m_ammoType;
  int m_damage{10}; // Base damage of this ammunition
};

#endif // CURRENCY_AND_GAME_RESOURCES_HPP