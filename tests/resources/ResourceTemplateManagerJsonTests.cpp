/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE ResourceTemplateManagerJsonTests
#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/Logger.hpp"
#include "entities/resources/CurrencyAndGameResources.hpp"
#include "entities/resources/ItemResources.hpp"
#include "entities/resources/MaterialResources.hpp"
#include "managers/ResourceTemplateManager.hpp"

// Helper function to find resource handle by name
HammerEngine::ResourceHandle
findResourceByName(ResourceTemplateManager *manager, const std::string &name) {
  // Use a more efficient approach - iterate through resource handles we know
  // exist rather than testing every possible handle ID
  for (int cat = 0; cat < static_cast<int>(ResourceCategory::COUNT); ++cat) {
    auto resources =
        manager->getResourcesByCategory(static_cast<ResourceCategory>(cat));
    for (const auto &resource : resources) {
      if (resource && resource->getName() == name) {
        return resource->getHandle();
      }
    }
  }
  return HammerEngine::ResourceHandle(); // Invalid handle
}

class ResourceTemplateManagerJsonTestFixture {
public:
  ResourceTemplateManagerJsonTestFixture() {
    // Initialize ResourceTemplateManager singleton
    resourceManager = &ResourceTemplateManager::Instance();
    BOOST_REQUIRE(resourceManager != nullptr);

    // Clean and initialize the manager for each test
    resourceManager->clean();
    bool initialized = resourceManager->init();
    BOOST_REQUIRE_MESSAGE(initialized,
                          "Failed to initialize ResourceTemplateManager");

    // Verify the manager is in a good state
    BOOST_TEST_MESSAGE("ResourceTemplateManager initialized with "
                       << resourceManager->getResourceTemplateCount()
                       << " default resources");
  }

  ~ResourceTemplateManagerJsonTestFixture() {
    // Clean up ResourceTemplateManager after each test
    resourceManager->clean();
  }

protected:
  ResourceTemplateManager *resourceManager;
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
            },
            {
                "id": "json_test_gem",
                "name": "JSON Test Gem",
                "category": "Currency",
                "type": "Gem",
                "description": "A gem loaded from JSON",
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

  // Get initial count
  size_t initialCount = resourceManager->getResourceTemplateCount();

  // Load resources from JSON string
  bool result = resourceManager->loadResourcesFromJsonString(jsonString);
  BOOST_CHECK_MESSAGE(result, "Failed to load resources from JSON string");

  // Verify resources were loaded
  size_t newCount = resourceManager->getResourceTemplateCount();
  BOOST_CHECK_EQUAL(newCount, initialCount + 3);

  // Test Equipment (sword)
  auto swordHandle = findResourceByName(resourceManager, "JSON Test Sword");
  BOOST_REQUIRE(swordHandle.isValid());
  auto sword = resourceManager->getResourceTemplate(swordHandle);
  BOOST_REQUIRE(sword != nullptr);
  BOOST_CHECK_EQUAL(sword->getName(), "JSON Test Sword");
  BOOST_CHECK_EQUAL(sword->getValue(), 150.0f);

  // Test Consumable (potion)
  auto potionHandle = findResourceByName(resourceManager, "JSON Test Potion");
  BOOST_REQUIRE(potionHandle.isValid());
  auto potion = resourceManager->getResourceTemplate(potionHandle);
  BOOST_REQUIRE(potion != nullptr);
  BOOST_CHECK_EQUAL(potion->getName(), "JSON Test Potion");
  BOOST_CHECK(potion->isConsumable());

  // Test Gem type casting and properties
  auto gemHandle = findResourceByName(resourceManager, "JSON Test Gem");
  BOOST_REQUIRE_MESSAGE(
      gemHandle.isValid(),
      "Failed to get handle for gem resource 'json_test_gem'");
  auto gem = resourceManager->getResourceTemplate(gemHandle);
  BOOST_REQUIRE_MESSAGE(gem != nullptr,
                        "Failed to retrieve gem resource 'json_test_gem'");
  BOOST_CHECK_EQUAL(gem->getName(), "JSON Test Gem");
  BOOST_TEST_MESSAGE("Retrieved gem: " << gem->getName() << " with value "
                                       << gem->getValue());

  // Test that they're the correct specialized types
  auto equipment = std::dynamic_pointer_cast<Equipment>(sword);
  BOOST_CHECK_MESSAGE(equipment != nullptr,
                      "Failed to cast sword to Equipment type");

  auto consumable = std::dynamic_pointer_cast<Consumable>(potion);
  BOOST_CHECK_MESSAGE(consumable != nullptr,
                      "Failed to cast potion to Consumable type");

  auto gemPtr = std::dynamic_pointer_cast<Gem>(gem);
  BOOST_REQUIRE_MESSAGE(
      gemPtr != nullptr,
      "Failed to cast resource to Gem type. Resource type might be: "
          << static_cast<int>(gem->getType()));

  BOOST_TEST_MESSAGE("Gem type: "
                     << static_cast<int>(gemPtr->getGemType()) << " (expected: "
                     << static_cast<int>(Gem::GemType::Diamond) << ")");
  BOOST_TEST_MESSAGE("Gem clarity: " << gemPtr->getClarity()
                                     << " (expected: 9)");

  BOOST_CHECK_EQUAL(static_cast<int>(gemPtr->getGemType()),
                    static_cast<int>(Gem::GemType::Diamond));
  BOOST_CHECK_EQUAL(gemPtr->getClarity(), 9);
}

BOOST_AUTO_TEST_CASE(TestLoadValidJsonFile) {
  // This test verifies that loadResourcesFromJson() properly delegates to
  // loadResourcesFromJsonString() after reading a file. Rather than creating
  // temporary files, we test with the existing JSON data files in the project.

  // Get initial count
  size_t initialCount = resourceManager->getResourceTemplateCount();
  BOOST_TEST_MESSAGE("Initial resource count: " << initialCount);

  // Test loading from the project's existing items.json file
  // Use std::filesystem for cross-platform path handling
  // This approach works on Windows (backslash), macOS/Linux (forward slash),
  // and handles different working directory scenarios automatically
  std::vector<std::filesystem::path> candidatePaths;

  // Try multiple potential working directories and path combinations
  std::filesystem::path itemsFile = "items.json";
  std::vector<std::filesystem::path> basePaths = {
      std::filesystem::current_path() / ".." / ".." / "res" /
          "data",                                       // From bin/debug/
      std::filesystem::current_path() / "res" / "data", // From project root
      std::filesystem::current_path() / ".." / "res" / "data", // From build/
      std::filesystem::path("res") / "data", // Relative from project root
      std::filesystem::path("..") / ".." / "res" /
          "data" // Relative from bin/debug/
  };

  // Build candidate paths with proper separators for the current platform
  for (const auto &basePath : basePaths) {
    candidatePaths.push_back(basePath / itemsFile);
  }

  bool result = false;
  std::string successfulPath;

  for (const auto &path : candidatePaths) {
    // Convert to string using native path separators
    std::string pathStr = path.string();

    // Check if file exists before trying to load it
    if (std::filesystem::exists(path) &&
        std::filesystem::is_regular_file(path)) {
      result = resourceManager->loadResourcesFromJson(pathStr);
      if (result) {
        successfulPath = pathStr;
        break;
      }
    }
  }

  BOOST_CHECK_MESSAGE(
      result, "Failed to load resources from items.json. Searched paths:\n" +
                  [&candidatePaths]() {
                    std::string pathList;
                    for (const auto &path : candidatePaths) {
                      pathList +=
                          "  - " + path.string() +
                          (std::filesystem::exists(path) ? " (exists)"
                                                         : " (not found)") +
                          "\n";
                    }
                    return pathList;
                  }());

  if (result) {
    // Verify resources were loaded from the file
    size_t newCount = resourceManager->getResourceTemplateCount();
    BOOST_TEST_MESSAGE(
        "New resource count after loading items.json: " << newCount);
    BOOST_CHECK_MESSAGE(
        newCount > initialCount,
        "Expected resource count to increase after loading items.json");

    BOOST_TEST_MESSAGE("Successfully loaded " << (newCount - initialCount)
                                              << " resources from "
                                              << successfulPath);
  } else {
    BOOST_TEST_MESSAGE(
        "Note: items.json may not exist or may have format differences. "
        "JSON string parsing is tested separately in TestLoadValidJsonString.");
  }
}

BOOST_AUTO_TEST_CASE(TestLoadInvalidJsonString) {
  // Test malformed JSON
  std::string invalidJson = R"({
        "resources": [
            {
                "id": "invalid_test",
                "name": "Invalid JSON",
                "category": "Item",
                "type": "Equipment"
                // Missing closing brace and comma
            }
        ]
    })";

  size_t initialCount = resourceManager->getResourceTemplateCount();
  bool result = resourceManager->loadResourcesFromJsonString(invalidJson);
  BOOST_CHECK_MESSAGE(!result, "Expected invalid JSON to fail parsing");

  // Verify no resources were added
  size_t newCount = resourceManager->getResourceTemplateCount();
  BOOST_CHECK_EQUAL(newCount, initialCount);
}

BOOST_AUTO_TEST_CASE(TestLoadEmptyJsonString) {
  // Test empty JSON
  std::string emptyJson = "{}";

  size_t initialCount = resourceManager->getResourceTemplateCount();
  bool result = resourceManager->loadResourcesFromJsonString(emptyJson);
  BOOST_CHECK_MESSAGE(!result,
                      "Expected empty JSON to fail (missing resources array)");

  // Verify no resources were added
  size_t newCount = resourceManager->getResourceTemplateCount();
  BOOST_CHECK_EQUAL(newCount, initialCount);
}

BOOST_AUTO_TEST_CASE(TestLoadInvalidResourceData) {
  // Test JSON with missing required fields
  std::string invalidResourceJson = R"({
        "resources": [
            {
                "id": "invalid_resource",
                "name": "Missing Category"
                // Missing category, type, etc.
            }
        ]
    })";

  size_t initialCount = resourceManager->getResourceTemplateCount();
  bool result =
      resourceManager->loadResourcesFromJsonString(invalidResourceJson);
  BOOST_CHECK_MESSAGE(!result, "Expected resource with missing fields to fail");

  // Verify no resources were added
  size_t newCount = resourceManager->getResourceTemplateCount();
  BOOST_CHECK_EQUAL(newCount, initialCount);
}

BOOST_AUTO_TEST_CASE(TestLoadNonExistentFile) {
  // Test loading from a file that doesn't exist
  std::string nonExistentFile = "../../non_existent_file.json";

  size_t initialCount = resourceManager->getResourceTemplateCount();
  bool result = resourceManager->loadResourcesFromJson(nonExistentFile);
  BOOST_CHECK_MESSAGE(!result, "Expected non-existent file to fail loading");

  // Verify no resources were added
  size_t newCount = resourceManager->getResourceTemplateCount();
  BOOST_CHECK_EQUAL(newCount, initialCount);
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

  auto resource1Handle = findResourceByName(resourceManager, "First Version");
  BOOST_REQUIRE(resource1Handle.isValid());
  auto resource1 = resourceManager->getResourceTemplate(resource1Handle);
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
  BOOST_CHECK(result2); // Should succeed - different resources can have same ID
                        // but different handles

  // Both resources should now exist with different names and handles
  auto firstHandle = findResourceByName(resourceManager, "First Version");
  BOOST_REQUIRE(firstHandle.isValid());
  auto firstResource = resourceManager->getResourceTemplate(firstHandle);
  BOOST_REQUIRE(firstResource != nullptr);
  BOOST_CHECK_EQUAL(firstResource->getName(), "First Version");

  auto secondHandle = findResourceByName(resourceManager, "Second Version");
  BOOST_REQUIRE(secondHandle.isValid());
  auto secondResource = resourceManager->getResourceTemplate(secondHandle);
  BOOST_REQUIRE(secondResource != nullptr);
  BOOST_CHECK_EQUAL(secondResource->getName(), "Second Version");
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