/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE ResourceTemplateManagerJsonTests
#include <boost/test/unit_test.hpp>

#include <fstream>
#include <memory>
#include <string>

#include "core/Logger.hpp"
#include "entities/resources/CurrencyAndGameResources.hpp"
#include "entities/resources/ItemResources.hpp"
#include "entities/resources/MaterialResources.hpp"
#include "managers/ResourceTemplateManager.hpp"

class ResourceTemplateManagerJsonTestFixture {
public:
  ResourceTemplateManagerJsonTestFixture() {
    // Initialize ResourceTemplateManager singleton
    resourceManager = &ResourceTemplateManager::Instance();
    BOOST_REQUIRE(resourceManager != nullptr);

    // Clean and initialize the manager
    resourceManager->clean();
    bool initialized = resourceManager->init();
    BOOST_REQUIRE(initialized);
  }

  ~ResourceTemplateManagerJsonTestFixture() {
    // Clean up ResourceTemplateManager
    resourceManager->clean();
  }

protected:
  ResourceTemplateManager *resourceManager;

  // Helper method to create a temporary JSON file
  std::string createTempJsonFile(const std::string &jsonContent) {
    std::string filename =
        "/tmp/test_resources_" + std::to_string(rand()) + ".json";
    std::ofstream file(filename);
    file << jsonContent;
    file.close();
    return filename;
  }

  // Helper method to remove temporary file
  void removeTempFile(const std::string &filename) {
    std::remove(filename.c_str());
  }
};

BOOST_FIXTURE_TEST_SUITE(ResourceTemplateManagerJsonTestSuite,
                         ResourceTemplateManagerJsonTestFixture)

BOOST_AUTO_TEST_CASE(TestLoadValidJsonString) {
  std::string jsonString = R"({
        "resources": [
            {
                "id": "json_test_sword",
                "name": "JSON Test Sword",
                "category": "Item",
                "type": "Equipment",
                "description": "A sword loaded from JSON",
                "value": 150,
                "maxStackSize": 1,
                "consumable": false,
                "properties": {
                    "slot": "Weapon",
                    "attackBonus": 20,
                    "defenseBonus": 0,
                    "speedBonus": 5
                }
            },
            {
                "id": "json_test_potion",
                "name": "JSON Test Potion",
                "category": "Item",
                "type": "Consumable",
                "description": "A potion loaded from JSON",
                "value": 75,
                "maxStackSize": 20,
                "consumable": true,
                "properties": {
                    "effect": "HealHP",
                    "effectPower": 75,
                    "effectDuration": 0
                }
            }
        ]
    })";

  // Get initial count
  size_t initialCount = resourceManager->getResourceTemplateCount();

  // Load resources from JSON string
  bool result = resourceManager->loadResourcesFromJsonString(jsonString);
  BOOST_CHECK(result);

  // Verify resources were loaded
  size_t newCount = resourceManager->getResourceTemplateCount();
  BOOST_CHECK_EQUAL(newCount, initialCount + 2);

  // Test that we can retrieve the loaded resources
  auto sword = resourceManager->getResourceTemplate("json_test_sword");
  BOOST_REQUIRE(sword != nullptr);
  BOOST_CHECK_EQUAL(sword->getName(), "JSON Test Sword");
  BOOST_CHECK_EQUAL(sword->getValue(), 150.0f);

  auto potion = resourceManager->getResourceTemplate("json_test_potion");
  BOOST_REQUIRE(potion != nullptr);
  BOOST_CHECK_EQUAL(potion->getName(), "JSON Test Potion");
  BOOST_CHECK(potion->isConsumable());

  // Test that they're the correct specialized types
  auto equipment = std::dynamic_pointer_cast<Equipment>(sword);
  BOOST_CHECK(equipment != nullptr);

  auto consumable = std::dynamic_pointer_cast<Consumable>(potion);
  BOOST_CHECK(consumable != nullptr);
}

BOOST_AUTO_TEST_CASE(TestLoadValidJsonFile) {
  std::string jsonContent = R"({
        "resources": [
            {
                "id": "file_test_gem",
                "name": "File Test Gem",
                "category": "Currency",
                "type": "Gem",
                "description": "A gem loaded from file",
                "value": 500,
                "maxStackSize": 100,
                "consumable": false,
                "properties": {
                    "gemType": "Diamond",
                    "exchangeRate": 500.0,
                    "clarity": 9
                }
            }
        ]
    })";

  // Create temporary file
  std::string filename = createTempJsonFile(jsonContent);

  try {
    // Get initial count
    size_t initialCount = resourceManager->getResourceTemplateCount();

    // Load resources from file
    bool result = resourceManager->loadResourcesFromJson(filename);
    BOOST_CHECK(result);

    // Verify resource was loaded
    size_t newCount = resourceManager->getResourceTemplateCount();
    BOOST_CHECK_EQUAL(newCount, initialCount + 1);

    // Test that we can retrieve the loaded resource
    auto gem = resourceManager->getResourceTemplate("file_test_gem");
    BOOST_REQUIRE(gem != nullptr);
    BOOST_CHECK_EQUAL(gem->getName(), "File Test Gem");

    // Test that it's the correct specialized type
    auto gemPtr = std::dynamic_pointer_cast<Gem>(gem);
    BOOST_REQUIRE(gemPtr != nullptr);
    BOOST_CHECK_EQUAL(static_cast<int>(gemPtr->getGemType()),
                      static_cast<int>(Gem::GemType::Diamond));
    BOOST_CHECK_EQUAL(gemPtr->getClarity(), 9);

  } catch (...) {
    removeTempFile(filename);
    throw;
  }

  removeTempFile(filename);
}

BOOST_AUTO_TEST_CASE(TestLoadDuplicateResources) {
  // First load
  std::string jsonString1 = R"({
        "resources": [
            {
                "id": "duplicate_test",
                "name": "First Version",
                "category": "Item",
                "type": "Equipment",
                "description": "First version of resource",
                "value": 100,
                "maxStackSize": 1,
                "consumable": false
            }
        ]
    })";

  bool result1 = resourceManager->loadResourcesFromJsonString(jsonString1);
  BOOST_CHECK(result1);

  auto resource1 = resourceManager->getResourceTemplate("duplicate_test");
  BOOST_REQUIRE(resource1 != nullptr);
  BOOST_CHECK_EQUAL(resource1->getName(), "First Version");

  // Second load with same ID (should fail to register)
  std::string jsonString2 = R"({
        "resources": [
            {
                "id": "duplicate_test",
                "name": "Second Version",
                "category": "Item",
                "type": "Equipment",
                "description": "Second version of resource",
                "value": 200,
                "maxStackSize": 1,
                "consumable": false
            }
        ]
    })";

  bool result2 = resourceManager->loadResourcesFromJsonString(jsonString2);
  BOOST_CHECK(!result2); // Should fail due to duplicate

  // Original resource should still be there unchanged
  auto resource2 = resourceManager->getResourceTemplate("duplicate_test");
  BOOST_REQUIRE(resource2 != nullptr);
  BOOST_CHECK_EQUAL(resource2->getName(),
                    "First Version"); // Should still be first version
}

BOOST_AUTO_TEST_CASE(TestLoadResourcesStatistics) {
  // Reset stats
  resourceManager->resetStats();
  ResourceStats initialStats = resourceManager->getStats();

  std::string jsonString = R"({
        "resources": [
            {
                "id": "stats_test_1",
                "name": "Stats Test 1",
                "category": "Item",
                "type": "Equipment",
                "value": 100,
                "maxStackSize": 1,
                "consumable": false
            },
            {
                "id": "stats_test_2",
                "name": "Stats Test 2",
                "category": "Material",
                "type": "RawResource",
                "value": 50,
                "maxStackSize": 100,
                "consumable": false
            }
        ]
    })";

  bool result = resourceManager->loadResourcesFromJsonString(jsonString);
  BOOST_CHECK(result);

  ResourceStats newStats = resourceManager->getStats();

  // Check that templates loaded count increased
  BOOST_CHECK(newStats.templatesLoaded.load() >
              initialStats.templatesLoaded.load());
}

BOOST_AUTO_TEST_SUITE_END()