/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE ResourceFactoryTests
#include <boost/test/unit_test.hpp>

#include <memory>
#include <string>

#include "core/Logger.hpp"
#include "entities/resources/CurrencyAndGameResources.hpp"
#include "entities/resources/ItemResources.hpp"
#include "entities/resources/MaterialResources.hpp"
#include "managers/ResourceFactory.hpp"
#include "utils/JsonReader.hpp"

using namespace HammerEngine;

class ResourceFactoryTestFixture {
public:
  ResourceFactoryTestFixture() {
    // Initialize the factory with default creators
    ResourceFactory::initialize();
  }

  ~ResourceFactoryTestFixture() {
    // Clean up factory
    ResourceFactory::clear();
  }

protected:
  // Helper method to create JsonValue from string
  JsonValue parseJson(const std::string &jsonString) {
    JsonReader reader;
    BOOST_REQUIRE(reader.parse(jsonString));
    return reader.getRoot();
  }
};

BOOST_FIXTURE_TEST_SUITE(ResourceFactoryTestSuite, ResourceFactoryTestFixture)

BOOST_AUTO_TEST_CASE(TestFactoryInitialization) {
  // Test that default creators are registered
  auto registeredTypes = ResourceFactory::getRegisteredTypes();
  BOOST_CHECK(registeredTypes.size() > 0);

  // Check for specific types
  BOOST_CHECK(ResourceFactory::hasCreator("Equipment"));
  BOOST_CHECK(ResourceFactory::hasCreator("Consumable"));
  BOOST_CHECK(ResourceFactory::hasCreator("QuestItem"));
  BOOST_CHECK(ResourceFactory::hasCreator("CraftingComponent"));
  BOOST_CHECK(ResourceFactory::hasCreator("RawResource"));
  BOOST_CHECK(ResourceFactory::hasCreator("Gold"));
  BOOST_CHECK(ResourceFactory::hasCreator("Gem"));
  BOOST_CHECK(ResourceFactory::hasCreator("Energy"));
  BOOST_CHECK(ResourceFactory::hasCreator("Mana"));
}

BOOST_AUTO_TEST_CASE(TestCreateEquipmentFromJson) {
  std::string jsonString = R"({
        "id": "test_sword",
        "name": "Test Sword",
        "category": "Item",
        "type": "Equipment",
        "description": "A test sword for testing",
        "value": 100,
        "maxStackSize": 1,
        "consumable": false,
        "properties": {
            "slot": "Weapon",
            "attackBonus": 15,
            "defenseBonus": 2,
            "speedBonus": 0,
            "durability": 100,
            "maxDurability": 100
        }
    })";

  JsonValue json = parseJson(jsonString);
  ResourcePtr resource = ResourceFactory::createFromJson(json);

  BOOST_REQUIRE(resource != nullptr);
  BOOST_CHECK_EQUAL(resource->getId(), "test_sword");
  BOOST_CHECK_EQUAL(resource->getName(), "Test Sword");
  BOOST_CHECK_EQUAL(static_cast<int>(resource->getCategory()),
                    static_cast<int>(ResourceCategory::Item));
  BOOST_CHECK_EQUAL(static_cast<int>(resource->getType()),
                    static_cast<int>(ResourceType::Equipment));

  // Test that it's actually an Equipment object
  auto equipment = std::dynamic_pointer_cast<Equipment>(resource);
  BOOST_REQUIRE(equipment != nullptr);
  BOOST_CHECK_EQUAL(static_cast<int>(equipment->getEquipmentSlot()),
                    static_cast<int>(Equipment::EquipmentSlot::Weapon));
  BOOST_CHECK_EQUAL(equipment->getAttackBonus(), 15);
  BOOST_CHECK_EQUAL(equipment->getDefenseBonus(), 2);
}

BOOST_AUTO_TEST_CASE(TestCreateConsumableFromJson) {
  std::string jsonString = R"({
        "id": "test_potion",
        "name": "Test Potion",
        "category": "Item",
        "type": "Consumable",
        "description": "A test healing potion",
        "value": 50,
        "maxStackSize": 10,
        "consumable": true,
        "properties": {
            "effect": "HealHP",
            "effectPower": 50,
            "effectDuration": 0
        }
    })";

  JsonValue json = parseJson(jsonString);
  ResourcePtr resource = ResourceFactory::createFromJson(json);

  BOOST_REQUIRE(resource != nullptr);
  BOOST_CHECK_EQUAL(resource->getId(), "test_potion");
  BOOST_CHECK_EQUAL(resource->getName(), "Test Potion");
  BOOST_CHECK(resource->isConsumable());

  // Test that it's actually a Consumable object
  auto consumable = std::dynamic_pointer_cast<Consumable>(resource);
  BOOST_REQUIRE(consumable != nullptr);
  BOOST_CHECK_EQUAL(static_cast<int>(consumable->getEffect()),
                    static_cast<int>(Consumable::ConsumableEffect::HealHP));
  BOOST_CHECK_EQUAL(consumable->getEffectPower(), 50);
  BOOST_CHECK_EQUAL(consumable->getEffectDuration(), 0);
}

BOOST_AUTO_TEST_CASE(TestCreateQuestItemFromJson) {
  std::string jsonString = R"({
        "id": "test_key",
        "name": "Test Key",
        "category": "Item",
        "type": "QuestItem",
        "description": "A key for testing purposes",
        "value": 0,
        "maxStackSize": 1,
        "consumable": false,
        "properties": {
            "questId": "test_quest_123"
        }
    })";

  JsonValue json = parseJson(jsonString);
  ResourcePtr resource = ResourceFactory::createFromJson(json);

  BOOST_REQUIRE(resource != nullptr);
  BOOST_CHECK_EQUAL(resource->getId(), "test_key");
  BOOST_CHECK_EQUAL(resource->getName(), "Test Key");

  // Test that it's actually a QuestItem object
  auto questItem = std::dynamic_pointer_cast<QuestItem>(resource);
  BOOST_REQUIRE(questItem != nullptr);
  BOOST_CHECK_EQUAL(questItem->getQuestId(), "test_quest_123");
  BOOST_CHECK(questItem->isQuestSpecific());
}

BOOST_AUTO_TEST_CASE(TestCreateCraftingComponentFromJson) {
  std::string jsonString = R"({
        "id": "test_essence",
        "name": "Test Essence",
        "category": "Material",
        "type": "CraftingComponent",
        "description": "A magical essence for testing",
        "value": 200,
        "maxStackSize": 50,
        "consumable": false,
        "properties": {
            "componentType": "Essence",
            "tier": 3,
            "purity": 0.8
        }
    })";

  JsonValue json = parseJson(jsonString);
  ResourcePtr resource = ResourceFactory::createFromJson(json);

  BOOST_REQUIRE(resource != nullptr);
  BOOST_CHECK_EQUAL(resource->getId(), "test_essence");
  BOOST_CHECK_EQUAL(resource->getName(), "Test Essence");

  // Test that it's actually a CraftingComponent object
  auto craftingComponent =
      std::dynamic_pointer_cast<CraftingComponent>(resource);
  BOOST_REQUIRE(craftingComponent != nullptr);
  BOOST_CHECK_EQUAL(
      static_cast<int>(craftingComponent->getComponentType()),
      static_cast<int>(CraftingComponent::ComponentType::Essence));
  BOOST_CHECK_EQUAL(craftingComponent->getTier(), 3);
  BOOST_CHECK_CLOSE(craftingComponent->getPurity(), 0.8f, 0.001f);
}

BOOST_AUTO_TEST_CASE(TestCreateRawResourceFromJson) {
  std::string jsonString = R"({
        "id": "test_ore",
        "name": "Test Ore",
        "category": "Material",
        "type": "RawResource",
        "description": "Raw ore for testing",
        "value": 25,
        "maxStackSize": 100,
        "consumable": false,
        "properties": {
            "origin": "Mining",
            "tier": 2,
            "rarity": 4
        }
    })";

  JsonValue json = parseJson(jsonString);
  ResourcePtr resource = ResourceFactory::createFromJson(json);

  BOOST_REQUIRE(resource != nullptr);
  BOOST_CHECK_EQUAL(resource->getId(), "test_ore");
  BOOST_CHECK_EQUAL(resource->getName(), "Test Ore");

  // Test that it's actually a RawResource object
  auto rawResource = std::dynamic_pointer_cast<RawResource>(resource);
  BOOST_REQUIRE(rawResource != nullptr);
  BOOST_CHECK_EQUAL(static_cast<int>(rawResource->getOrigin()),
                    static_cast<int>(RawResource::ResourceOrigin::Mining));
  BOOST_CHECK_EQUAL(rawResource->getTier(), 2);
  BOOST_CHECK_EQUAL(rawResource->getRarity(), 4);
}

BOOST_AUTO_TEST_CASE(TestCreateGoldFromJson) {
  std::string jsonString = R"({
        "id": "test_gold",
        "name": "Test Gold",
        "category": "Currency",
        "type": "Gold",
        "description": "Gold coins for testing",
        "value": 1,
        "maxStackSize": 10000,
        "consumable": false,
        "properties": {
            "exchangeRate": 1.0
        }
    })";

  JsonValue json = parseJson(jsonString);
  ResourcePtr resource = ResourceFactory::createFromJson(json);

  BOOST_REQUIRE(resource != nullptr);
  BOOST_CHECK_EQUAL(resource->getId(), "test_gold");
  BOOST_CHECK_EQUAL(resource->getName(), "Test Gold");

  // Test that it's actually a Gold object
  auto gold = std::dynamic_pointer_cast<Gold>(resource);
  BOOST_REQUIRE(gold != nullptr);
  BOOST_CHECK_CLOSE(gold->getExchangeRate(), 1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(TestCreateGemFromJson) {
  std::string jsonString = R"({
        "id": "test_emerald",
        "name": "Test Emerald",
        "category": "Currency",
        "type": "Gem",
        "description": "Emerald gem for testing",
        "value": 100,
        "maxStackSize": 1000,
        "consumable": false,
        "properties": {
            "gemType": "Emerald",
            "exchangeRate": 100.0,
            "clarity": 8
        }
    })";

  JsonValue json = parseJson(jsonString);
  ResourcePtr resource = ResourceFactory::createFromJson(json);

  BOOST_REQUIRE(resource != nullptr);
  BOOST_CHECK_EQUAL(resource->getId(), "test_emerald");
  BOOST_CHECK_EQUAL(resource->getName(), "Test Emerald");

  // Test that it's actually a Gem object
  auto gem = std::dynamic_pointer_cast<Gem>(resource);
  BOOST_REQUIRE(gem != nullptr);
  BOOST_CHECK_EQUAL(static_cast<int>(gem->getGemType()),
                    static_cast<int>(Gem::GemType::Emerald));
  BOOST_CHECK_CLOSE(gem->getExchangeRate(), 100.0f, 0.001f);
  BOOST_CHECK_EQUAL(gem->getClarity(), 8);
}

BOOST_AUTO_TEST_CASE(TestCreateEnergyFromJson) {
  std::string jsonString = R"({
        "id": "test_energy",
        "name": "Test Energy",
        "category": "GameResource",
        "type": "Energy",
        "description": "Energy for testing",
        "value": 0,
        "maxStackSize": 999999,
        "consumable": false,
        "properties": {
            "regenerationRate": 1.5,
            "maxEnergy": 200
        }
    })";

  JsonValue json = parseJson(jsonString);
  ResourcePtr resource = ResourceFactory::createFromJson(json);

  BOOST_REQUIRE(resource != nullptr);
  BOOST_CHECK_EQUAL(resource->getId(), "test_energy");
  BOOST_CHECK_EQUAL(resource->getName(), "Test Energy");

  // Test that it's actually an Energy object
  auto energy = std::dynamic_pointer_cast<Energy>(resource);
  BOOST_REQUIRE(energy != nullptr);
  BOOST_CHECK_CLOSE(energy->getRegenerationRate(), 1.5f, 0.001f);
  BOOST_CHECK_EQUAL(energy->getMaxEnergy(), 200);
}

BOOST_AUTO_TEST_CASE(TestCreateManaFromJson) {
  std::string jsonString = R"({
        "id": "test_mana",
        "name": "Test Mana",
        "category": "GameResource",
        "type": "Mana",
        "description": "Mana for testing",
        "value": 0,
        "maxStackSize": 10000,
        "consumable": false,
        "properties": {
            "manaType": "Divine",
            "regenerationRate": 0.5,
            "maxMana": 150
        }
    })";

  JsonValue json = parseJson(jsonString);
  ResourcePtr resource = ResourceFactory::createFromJson(json);

  BOOST_REQUIRE(resource != nullptr);
  BOOST_CHECK_EQUAL(resource->getId(), "test_mana");
  BOOST_CHECK_EQUAL(resource->getName(), "Test Mana");

  // Test that it's actually a Mana object
  auto mana = std::dynamic_pointer_cast<Mana>(resource);
  BOOST_REQUIRE(mana != nullptr);
  BOOST_CHECK_EQUAL(static_cast<int>(mana->getManaType()),
                    static_cast<int>(Mana::ManaType::Divine));
  BOOST_CHECK_CLOSE(mana->getRegenerationRate(), 0.5f, 0.001f);
  BOOST_CHECK_EQUAL(mana->getMaxMana(), 150);
}

BOOST_AUTO_TEST_CASE(TestInvalidJsonHandling) {
  // Test empty JSON
  std::string emptyJson = "{}";
  JsonValue json = parseJson(emptyJson);
  ResourcePtr resource = ResourceFactory::createFromJson(json);
  BOOST_CHECK(resource == nullptr);

  // Test missing required fields
  std::string incompleteJson = R"({"id": "test", "name": "Test"})";
  json = parseJson(incompleteJson);
  resource = ResourceFactory::createFromJson(json);
  BOOST_CHECK(resource == nullptr);

  // Test invalid JSON type (not object)
  std::string invalidJson = R"("not an object")";
  json = parseJson(invalidJson);
  resource = ResourceFactory::createFromJson(json);
  BOOST_CHECK(resource == nullptr);
}

BOOST_AUTO_TEST_CASE(TestUnknownTypeHandling) {
  std::string jsonString = R"({
        "id": "test_unknown",
        "name": "Test Unknown",
        "category": "Item",
        "type": "UnknownType",
        "description": "Unknown type for testing",
        "value": 10,
        "maxStackSize": 1,
        "consumable": false
    })";

  JsonValue json = parseJson(jsonString);
  ResourcePtr resource = ResourceFactory::createFromJson(json);

  // Should fallback to base Resource class
  BOOST_REQUIRE(resource != nullptr);
  BOOST_CHECK_EQUAL(resource->getId(), "test_unknown");
  BOOST_CHECK_EQUAL(resource->getName(), "Test Unknown");
}

BOOST_AUTO_TEST_CASE(TestCustomCreatorRegistration) {
  // Test registering a custom creator
  bool registered = ResourceFactory::registerCreator(
      "CustomType", [](const JsonValue &json) -> ResourcePtr {
        return std::make_shared<Resource>(
            json["id"].asString(), json["name"].asString(),
            ResourceCategory::Item, ResourceType::Equipment);
      });

  BOOST_CHECK(registered);
  BOOST_CHECK(ResourceFactory::hasCreator("CustomType"));

  // Test that registering the same type again fails
  bool registeredAgain = ResourceFactory::registerCreator(
      "CustomType",
      [](const JsonValue & /*json*/) -> ResourcePtr { return nullptr; });

  BOOST_CHECK(!registeredAgain);
}

BOOST_AUTO_TEST_CASE(TestFactoryClear) {
  // Verify we have creators
  BOOST_CHECK(ResourceFactory::getRegisteredTypes().size() > 0);

  // Clear factory
  ResourceFactory::clear();

  // Verify factory is empty
  BOOST_CHECK(ResourceFactory::getRegisteredTypes().size() == 0);
  BOOST_CHECK(!ResourceFactory::hasCreator("Equipment"));

  // Re-initialize for cleanup
  ResourceFactory::initialize();
}

BOOST_AUTO_TEST_SUITE_END()