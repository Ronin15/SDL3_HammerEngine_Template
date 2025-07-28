/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/ResourceFactory.hpp"
#include "core/Logger.hpp"
#include "entities/resources/CurrencyAndGameResources.hpp"
#include "entities/resources/ItemResources.hpp"
#include "entities/resources/MaterialResources.hpp"

namespace HammerEngine {

ResourcePtr ResourceFactory::createFromJson(const JsonValue &json) {
  if (!json.isObject()) {
    RESOURCE_ERROR(
        "ResourceFactory::createFromJson - Invalid JSON: not an object");
    return nullptr;
  }

  // Extract required fields
  if (!json.hasKey("id") || !json.hasKey("name") || !json.hasKey("category") ||
      !json.hasKey("type")) {
    RESOURCE_ERROR("ResourceFactory::createFromJson - Missing required fields "
                   "(id, name, category, type)");
    return nullptr;
  }

  std::string id = json["id"].tryAsString().value_or("");
  std::string name = json["name"].tryAsString().value_or("");
  std::string categoryStr = json["category"].tryAsString().value_or("");
  std::string typeStr = json["type"].tryAsString().value_or("");

  if (id.empty() || name.empty() || categoryStr.empty() || typeStr.empty()) {
    RESOURCE_ERROR("ResourceFactory::createFromJson - Empty required fields");
    return nullptr;
  }

  // Convert strings to enums for potential fallback use
  ResourceCategory category = Resource::stringToCategory(categoryStr);
  ResourceType type = Resource::stringToType(typeStr);

  // Look for specialized creator based on type
  auto &creators = getCreators();
  auto creatorIt = creators.find(typeStr);
  if (creatorIt != creators.end()) {
    try {
      ResourcePtr resource = creatorIt->second(json);
      if (resource) {
        RESOURCE_DEBUG("ResourceFactory::createFromJson - Created " + typeStr +
                       " resource: " + id);
        return resource;
      }
    } catch (const std::exception &ex) {
      RESOURCE_ERROR("ResourceFactory::createFromJson - Exception creating " +
                     typeStr + ": " + std::string(ex.what()));
    }
  }

  // Fallback to base Resource creation using the converted enums
  RESOURCE_WARN(
      "ResourceFactory::createFromJson - No specialized creator for type '" +
      typeStr + "', creating base Resource with category " + categoryStr);

  auto resource = std::make_shared<Resource>(id, name, category, type);
  setCommonProperties(resource, json);
  return resource;
}

bool ResourceFactory::registerCreator(const std::string &typeName,
                                      ResourceCreator creator) {
  auto &creators = getCreators();
  if (creators.find(typeName) != creators.end()) {
    RESOURCE_WARN("ResourceFactory::registerCreator - Type '" + typeName +
                  "' already registered");
    return false;
  }

  creators[typeName] = creator;
  RESOURCE_DEBUG(
      "ResourceFactory::registerCreator - Registered creator for type: " +
      typeName);
  return true;
}

bool ResourceFactory::hasCreator(const std::string &typeName) {
  auto &creators = getCreators();
  return creators.find(typeName) != creators.end();
}

std::vector<std::string> ResourceFactory::getRegisteredTypes() {
  std::vector<std::string> types;
  const auto &creators = getCreators();
  types.reserve(creators.size());

  for (const auto &[typeName, creator] : creators) {
    types.push_back(typeName);
  }

  return types;
}

void ResourceFactory::initialize() {
  RESOURCE_INFO(
      "ResourceFactory::initialize - Registering default resource creators");

  // Register creators for all resource types
  registerCreator("Equipment", createEquipment);
  registerCreator("Consumable", createConsumable);
  registerCreator("QuestItem", createQuestItem);
  registerCreator("CraftingComponent", createMaterial);
  registerCreator("RawResource", createMaterial);
  registerCreator("Gold", createCurrency);
  registerCreator("Gem", createCurrency);
  registerCreator("FactionToken", createCurrency);
  registerCreator("Energy", createGameResource);
  registerCreator("Mana", createGameResource);
  registerCreator("BuildingMaterial", createGameResource);
  registerCreator("Ammunition", createGameResource);

  RESOURCE_INFO("ResourceFactory::initialize - Registered " +
                std::to_string(getCreators().size()) + " resource creators");
}

void ResourceFactory::clear() {
  auto &creators = getCreators();
  creators.clear();
  RESOURCE_DEBUG("ResourceFactory::clear - Cleared all resource creators");
}

ResourcePtr ResourceFactory::createBaseResource(const JsonValue &json) {
  std::string id = json["id"].asString();
  std::string name = json["name"].asString();
  std::string categoryStr = json["category"].asString();
  std::string typeStr = json["type"].asString();

  ResourceCategory category = Resource::stringToCategory(categoryStr);
  ResourceType type = Resource::stringToType(typeStr);

  auto resource = std::make_shared<Resource>(id, name, category, type);
  setCommonProperties(resource, json);

  return resource;
}

ResourcePtr ResourceFactory::createEquipment(const JsonValue &json) {
  std::string id = json["id"].asString();
  std::string name = json["name"].asString();

  // Determine equipment slot from JSON or default to Weapon
  Equipment::EquipmentSlot slot = Equipment::EquipmentSlot::Weapon;
  if (json.hasKey("properties") && json["properties"].isObject()) {
    const JsonValue &props = json["properties"];
    if (props.hasKey("slot")) {
      std::string slotStr = props["slot"].tryAsString().value_or("Weapon");
      // Simple mapping - could be enhanced with a conversion utility
      if (slotStr == "Helmet")
        slot = Equipment::EquipmentSlot::Helmet;
      else if (slotStr == "Chest")
        slot = Equipment::EquipmentSlot::Chest;
      else if (slotStr == "Legs")
        slot = Equipment::EquipmentSlot::Legs;
      else if (slotStr == "Boots")
        slot = Equipment::EquipmentSlot::Boots;
      else if (slotStr == "Gloves")
        slot = Equipment::EquipmentSlot::Gloves;
      else if (slotStr == "Ring")
        slot = Equipment::EquipmentSlot::Ring;
      else if (slotStr == "Necklace")
        slot = Equipment::EquipmentSlot::Necklace;
    }
  }

  auto equipment = std::make_shared<Equipment>(id, name, slot);
  setCommonProperties(equipment, json);

  // Set equipment-specific properties
  if (json.hasKey("properties") && json["properties"].isObject()) {
    const JsonValue &props = json["properties"];
    equipment->setAttackBonus(props["attackBonus"].tryAsInt().value_or(0));
    equipment->setDefenseBonus(props["defenseBonus"].tryAsInt().value_or(0));
    equipment->setSpeedBonus(props["speedBonus"].tryAsInt().value_or(0));

    if (props.hasKey("durability") && props.hasKey("maxDurability")) {
      equipment->setDurability(props["durability"].tryAsInt().value_or(100),
                               props["maxDurability"].tryAsInt().value_or(100));
    }
  }

  return equipment;
}

ResourcePtr ResourceFactory::createConsumable(const JsonValue &json) {
  std::string id = json["id"].asString();
  std::string name = json["name"].asString();

  auto consumable = std::make_shared<Consumable>(id, name);
  setCommonProperties(consumable, json);

  // Set consumable-specific properties
  if (json.hasKey("properties") && json["properties"].isObject()) {
    const JsonValue &props = json["properties"];

    // Map effect string to enum
    if (props.hasKey("effect")) {
      std::string effectStr = props["effect"].tryAsString().value_or("HealHP");
      Consumable::ConsumableEffect effect =
          Consumable::ConsumableEffect::HealHP;

      if (effectStr == "RestoreMP")
        effect = Consumable::ConsumableEffect::RestoreMP;
      else if (effectStr == "BoostAttack")
        effect = Consumable::ConsumableEffect::BoostAttack;
      else if (effectStr == "BoostDefense")
        effect = Consumable::ConsumableEffect::BoostDefense;
      else if (effectStr == "BoostSpeed")
        effect = Consumable::ConsumableEffect::BoostSpeed;
      else if (effectStr == "Teleport")
        effect = Consumable::ConsumableEffect::Teleport;

      consumable->setEffect(effect);
    }

    consumable->setEffectPower(props["effectPower"].tryAsInt().value_or(10));
    consumable->setEffectDuration(
        props["effectDuration"].tryAsInt().value_or(0));
  }

  return consumable;
}

ResourcePtr ResourceFactory::createQuestItem(const JsonValue &json) {
  std::string id = json["id"].asString();
  std::string name = json["name"].asString();
  std::string questId = "";

  if (json.hasKey("properties") && json["properties"].isObject()) {
    const JsonValue &props = json["properties"];
    questId = props["questId"].tryAsString().value_or("");
  }

  auto questItem = std::make_shared<QuestItem>(id, name, questId);
  setCommonProperties(questItem, json);

  return questItem;
}

ResourcePtr ResourceFactory::createMaterial(const JsonValue &json) {
  std::string id = json["id"].asString();
  std::string name = json["name"].asString();
  std::string typeStr = json["type"].asString();

  ResourceType type = Resource::stringToType(typeStr);

  if (type == ResourceType::CraftingComponent) {
    // Determine component type from JSON or default to Metal
    CraftingComponent::ComponentType componentType =
        CraftingComponent::ComponentType::Metal;
    if (json.hasKey("properties") && json["properties"].isObject()) {
      const JsonValue &props = json["properties"];
      if (props.hasKey("componentType")) {
        std::string compTypeStr =
            props["componentType"].tryAsString().value_or("Metal");
        if (compTypeStr == "Wood")
          componentType = CraftingComponent::ComponentType::Wood;
        else if (compTypeStr == "Leather")
          componentType = CraftingComponent::ComponentType::Leather;
        else if (compTypeStr == "Fabric")
          componentType = CraftingComponent::ComponentType::Fabric;
        else if (compTypeStr == "Gem")
          componentType = CraftingComponent::ComponentType::Gem;
        else if (compTypeStr == "Essence")
          componentType = CraftingComponent::ComponentType::Essence;
        else if (compTypeStr == "Crystal")
          componentType = CraftingComponent::ComponentType::Crystal;
      }
    }

    auto craftingComponent =
        std::make_shared<CraftingComponent>(id, name, componentType);
    setCommonProperties(craftingComponent, json);

    // Set crafting component specific properties
    if (json.hasKey("properties") && json["properties"].isObject()) {
      const JsonValue &props = json["properties"];
      craftingComponent->setTier(props["tier"].tryAsInt().value_or(1));
      craftingComponent->setPurity(
          static_cast<float>(props["purity"].tryAsNumber().value_or(1.0)));
    }

    return craftingComponent;
  } else if (type == ResourceType::RawResource) {
    // Determine resource origin from JSON or default to Mining
    RawResource::ResourceOrigin origin = RawResource::ResourceOrigin::Mining;
    if (json.hasKey("properties") && json["properties"].isObject()) {
      const JsonValue &props = json["properties"];
      if (props.hasKey("origin")) {
        std::string originStr =
            props["origin"].tryAsString().value_or("Mining");
        if (originStr == "Logging")
          origin = RawResource::ResourceOrigin::Logging;
        else if (originStr == "Harvesting")
          origin = RawResource::ResourceOrigin::Harvesting;
        else if (originStr == "Hunting")
          origin = RawResource::ResourceOrigin::Hunting;
        else if (originStr == "Fishing")
          origin = RawResource::ResourceOrigin::Fishing;
        else if (originStr == "Monster")
          origin = RawResource::ResourceOrigin::Monster;
      }
    }

    auto rawResource = std::make_shared<RawResource>(id, name, origin);
    setCommonProperties(rawResource, json);

    // Set raw resource specific properties
    if (json.hasKey("properties") && json["properties"].isObject()) {
      const JsonValue &props = json["properties"];
      rawResource->setTier(props["tier"].tryAsInt().value_or(1));
      rawResource->setRarity(props["rarity"].tryAsInt().value_or(1));
    }

    return rawResource;
  }

  // Fallback to base Material class
  auto material = std::make_shared<Material>(id, name, type);
  setCommonProperties(material, json);

  if (json.hasKey("properties") && json["properties"].isObject()) {
    const JsonValue &props = json["properties"];
    material->setTier(props["tier"].tryAsInt().value_or(1));
  }

  return material;
}
ResourcePtr ResourceFactory::createCurrency(const JsonValue &json) {
  std::string id = json["id"].asString();
  std::string name = json["name"].asString();
  std::string typeStr = json["type"].asString();

  ResourceType type = Resource::stringToType(typeStr);

  if (type == ResourceType::Gold) {
    auto gold = std::make_shared<Gold>(id, name);
    setCommonProperties(gold, json);

    // Set currency-specific properties
    if (json.hasKey("properties") && json["properties"].isObject()) {
      const JsonValue &props = json["properties"];
      gold->setExchangeRate(static_cast<float>(
          props["exchangeRate"].tryAsNumber().value_or(1.0)));
    }

    return gold;
  } else if (type == ResourceType::Gem) {
    // Determine gem type from JSON or default to Ruby
    Gem::GemType gemType = Gem::GemType::Ruby;
    if (json.hasKey("properties") && json["properties"].isObject()) {
      const JsonValue &props = json["properties"];
      if (props.hasKey("gemType")) {
        std::string gemTypeStr =
            props["gemType"].tryAsString().value_or("Ruby");
        if (gemTypeStr == "Emerald")
          gemType = Gem::GemType::Emerald;
        else if (gemTypeStr == "Sapphire")
          gemType = Gem::GemType::Sapphire;
        else if (gemTypeStr == "Diamond")
          gemType = Gem::GemType::Diamond;
      }
    }

    auto gem = std::make_shared<Gem>(id, name, gemType);
    setCommonProperties(gem, json);

    // Set gem-specific properties
    if (json.hasKey("properties") && json["properties"].isObject()) {
      const JsonValue &props = json["properties"];
      gem->setExchangeRate(static_cast<float>(
          props["exchangeRate"].tryAsNumber().value_or(1.0)));
      gem->setClarity(props["clarity"].tryAsInt().value_or(5));
    }

    return gem;
  } else if (type == ResourceType::FactionToken) {
    std::string factionId = "";
    if (json.hasKey("properties") && json["properties"].isObject()) {
      const JsonValue &props = json["properties"];
      factionId = props["factionId"].tryAsString().value_or("");
    }

    auto factionToken = std::make_shared<FactionToken>(id, name, factionId);
    setCommonProperties(factionToken, json);

    // Set faction token specific properties
    if (json.hasKey("properties") && json["properties"].isObject()) {
      const JsonValue &props = json["properties"];
      factionToken->setExchangeRate(static_cast<float>(
          props["exchangeRate"].tryAsNumber().value_or(1.0)));
      factionToken->setReputation(props["reputation"].tryAsInt().value_or(0));
    }

    return factionToken;
  }

  // Fallback to base Currency class
  auto currency = std::make_shared<Currency>(id, name, type);
  setCommonProperties(currency, json);

  if (json.hasKey("properties") && json["properties"].isObject()) {
    const JsonValue &props = json["properties"];
    currency->setExchangeRate(
        static_cast<float>(props["exchangeRate"].tryAsNumber().value_or(1.0)));
  }

  return currency;
}
ResourcePtr ResourceFactory::createGameResource(const JsonValue &json) {
  std::string id = json["id"].asString();
  std::string name = json["name"].asString();
  std::string typeStr = json["type"].asString();

  ResourceType type = Resource::stringToType(typeStr);

  if (type == ResourceType::Energy) {
    auto energy = std::make_shared<Energy>(id, name);
    setCommonProperties(energy, json);

    // Set energy-specific properties
    if (json.hasKey("properties") && json["properties"].isObject()) {
      const JsonValue &props = json["properties"];
      energy->setRegenerationRate(static_cast<float>(
          props["regenerationRate"].tryAsNumber().value_or(0.0)));
      energy->setMaxEnergy(props["maxEnergy"].tryAsInt().value_or(100));
    }

    return energy;
  } else if (type == ResourceType::Mana) {
    // Determine mana type from JSON or default to Arcane
    Mana::ManaType manaType = Mana::ManaType::Arcane;
    if (json.hasKey("properties") && json["properties"].isObject()) {
      const JsonValue &props = json["properties"];
      if (props.hasKey("manaType")) {
        std::string manaTypeStr =
            props["manaType"].tryAsString().value_or("Arcane");
        if (manaTypeStr == "Divine")
          manaType = Mana::ManaType::Divine;
        else if (manaTypeStr == "Nature")
          manaType = Mana::ManaType::Nature;
        else if (manaTypeStr == "Dark")
          manaType = Mana::ManaType::Dark;
      }
    }

    auto mana = std::make_shared<Mana>(id, name, manaType);
    setCommonProperties(mana, json);

    // Set mana-specific properties
    if (json.hasKey("properties") && json["properties"].isObject()) {
      const JsonValue &props = json["properties"];
      mana->setRegenerationRate(static_cast<float>(
          props["regenerationRate"].tryAsNumber().value_or(0.0)));
      mana->setMaxMana(props["maxMana"].tryAsInt().value_or(100));
    }

    return mana;
  } else if (type == ResourceType::BuildingMaterial) {
    // Determine material type from JSON or default to Wood
    BuildingMaterial::MaterialType materialType =
        BuildingMaterial::MaterialType::Wood;
    if (json.hasKey("properties") && json["properties"].isObject()) {
      const JsonValue &props = json["properties"];
      if (props.hasKey("materialType")) {
        std::string matTypeStr =
            props["materialType"].tryAsString().value_or("Wood");
        if (matTypeStr == "Stone")
          materialType = BuildingMaterial::MaterialType::Stone;
        else if (matTypeStr == "Metal")
          materialType = BuildingMaterial::MaterialType::Metal;
        else if (matTypeStr == "Crystal")
          materialType = BuildingMaterial::MaterialType::Crystal;
      }
    }

    auto buildingMaterial =
        std::make_shared<BuildingMaterial>(id, name, materialType);
    setCommonProperties(buildingMaterial, json);

    // Set building material specific properties
    if (json.hasKey("properties") && json["properties"].isObject()) {
      const JsonValue &props = json["properties"];
      buildingMaterial->setRegenerationRate(static_cast<float>(
          props["regenerationRate"].tryAsNumber().value_or(0.0)));
      buildingMaterial->setDurability(
          props["durability"].tryAsInt().value_or(100));
    }

    return buildingMaterial;
  } else if (type == ResourceType::Ammunition) {
    // Determine ammo type from JSON or default to Arrow
    Ammunition::AmmoType ammoType = Ammunition::AmmoType::Arrow;
    if (json.hasKey("properties") && json["properties"].isObject()) {
      const JsonValue &props = json["properties"];
      if (props.hasKey("ammoType")) {
        std::string ammoTypeStr =
            props["ammoType"].tryAsString().value_or("Arrow");
        if (ammoTypeStr == "Bolt")
          ammoType = Ammunition::AmmoType::Bolt;
        else if (ammoTypeStr == "Bullet")
          ammoType = Ammunition::AmmoType::Bullet;
        else if (ammoTypeStr == "ThrowingKnife")
          ammoType = Ammunition::AmmoType::ThrowingKnife;
        else if (ammoTypeStr == "MagicMissile")
          ammoType = Ammunition::AmmoType::MagicMissile;
      }
    }

    auto ammunition = std::make_shared<Ammunition>(id, name, ammoType);
    setCommonProperties(ammunition, json);

    // Set ammunition-specific properties
    if (json.hasKey("properties") && json["properties"].isObject()) {
      const JsonValue &props = json["properties"];
      ammunition->setRegenerationRate(static_cast<float>(
          props["regenerationRate"].tryAsNumber().value_or(0.0)));
      ammunition->setDamage(props["damage"].tryAsInt().value_or(10));
    }

    return ammunition;
  }

  // Fallback to base GameResource class
  auto gameResource = std::make_shared<GameResource>(id, name, type);
  setCommonProperties(gameResource, json);

  if (json.hasKey("properties") && json["properties"].isObject()) {
    const JsonValue &props = json["properties"];
    gameResource->setRegenerationRate(static_cast<float>(
        props["regenerationRate"].tryAsNumber().value_or(0.0)));
  }

  return gameResource;
}
void ResourceFactory::setCommonProperties(ResourcePtr resource,
                                          const JsonValue &json) {
  if (!resource)
    return;

  // Set basic properties
  if (json.hasKey("description")) {
    resource->setDescription(json["description"].tryAsString().value_or(""));
  }

  if (json.hasKey("value")) {
    resource->setValue(
        static_cast<float>(json["value"].tryAsNumber().value_or(0.0)));
  }

  if (json.hasKey("maxStackSize")) {
    resource->setMaxStackSize(json["maxStackSize"].tryAsInt().value_or(1));
  }

  if (json.hasKey("consumable")) {
    resource->setConsumable(json["consumable"].tryAsBool().value_or(false));
  }

  if (json.hasKey("iconTextureId")) {
    resource->setIconTextureId(
        json["iconTextureId"].tryAsString().value_or(""));
  }
}

} // namespace HammerEngine