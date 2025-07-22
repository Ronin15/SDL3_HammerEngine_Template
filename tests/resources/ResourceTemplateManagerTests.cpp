/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE ResourceTemplateManagerTests
#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/Logger.hpp"
#include "entities/Resource.hpp"
#include "entities/resources/CurrencyAndGameResources.hpp"
#include "entities/resources/ItemResources.hpp"
#include "entities/resources/MaterialResources.hpp"
#include "managers/ResourceTemplateManager.hpp"

class ResourceTemplateManagerTestFixture {
public:
  ResourceTemplateManagerTestFixture() {
    // Initialize ResourceTemplateManager singleton
    resourceManager = &ResourceTemplateManager::Instance();
    BOOST_REQUIRE(resourceManager != nullptr);

    // Initialize the manager (loads default resources)
    bool initialized = resourceManager->init();
    BOOST_REQUIRE(initialized);
  }

  ~ResourceTemplateManagerTestFixture() {
    // Clean up ResourceTemplateManager
    resourceManager->clean();
  }

protected:
  ResourceTemplateManager *resourceManager;
};

BOOST_FIXTURE_TEST_SUITE(ResourceTemplateManagerTestSuite,
                         ResourceTemplateManagerTestFixture)

BOOST_AUTO_TEST_CASE(TestSingletonPattern) {
  // Test that Instance always returns the same instance
  ResourceTemplateManager *instance1 = &ResourceTemplateManager::Instance();
  ResourceTemplateManager *instance2 = &ResourceTemplateManager::Instance();

  BOOST_CHECK(instance1 == instance2);
  BOOST_CHECK(instance1 == resourceManager);
}

BOOST_AUTO_TEST_CASE(TestInitialization) {
  // Test that ResourceManager is initialized
  BOOST_CHECK(resourceManager->isInitialized());

  // Test that we have some default resources loaded
  BOOST_CHECK(resourceManager->getResourceTemplateCount() > 0);

  // Clean and re-initialize
  resourceManager->clean();
  BOOST_CHECK(!resourceManager->isInitialized());

  bool reinitialized = resourceManager->init();
  BOOST_CHECK(reinitialized);
  BOOST_CHECK(resourceManager->isInitialized());
}

BOOST_AUTO_TEST_CASE(TestDefaultResourcesLoaded) {
  // Test that default resources are properly loaded

  // Test item resources
  auto healthPotion = resourceManager->getResourceTemplate("health_potion");
  BOOST_REQUIRE(healthPotion != nullptr);
  BOOST_CHECK_EQUAL(healthPotion->getName(), "Health Potion");
  BOOST_CHECK_EQUAL(static_cast<int>(healthPotion->getCategory()),
                    static_cast<int>(ResourceCategory::Item));
  BOOST_CHECK_EQUAL(static_cast<int>(healthPotion->getType()),
                    static_cast<int>(ResourceType::Consumable));

  auto ironSword = resourceManager->getResourceTemplate("iron_sword");
  BOOST_REQUIRE(ironSword != nullptr);
  BOOST_CHECK_EQUAL(ironSword->getName(), "Iron Sword");
  BOOST_CHECK_EQUAL(static_cast<int>(ironSword->getCategory()),
                    static_cast<int>(ResourceCategory::Item));
  BOOST_CHECK_EQUAL(static_cast<int>(ironSword->getType()),
                    static_cast<int>(ResourceType::Equipment));

  // Test material resources
  auto ironOre = resourceManager->getResourceTemplate("iron_ore");
  BOOST_REQUIRE(ironOre != nullptr);
  BOOST_CHECK_EQUAL(ironOre->getName(), "Iron Ore");
  BOOST_CHECK_EQUAL(static_cast<int>(ironOre->getCategory()),
                    static_cast<int>(ResourceCategory::Material));
  BOOST_CHECK_EQUAL(static_cast<int>(ironOre->getType()),
                    static_cast<int>(ResourceType::RawResource));

  // Test currency resources
  auto gold = resourceManager->getResourceTemplate("gold");
  BOOST_REQUIRE(gold != nullptr);
  BOOST_CHECK_EQUAL(gold->getName(), "Gold Coins");
  BOOST_CHECK_EQUAL(static_cast<int>(gold->getCategory()),
                    static_cast<int>(ResourceCategory::Currency));
  BOOST_CHECK_EQUAL(static_cast<int>(gold->getType()),
                    static_cast<int>(ResourceType::Gold));
}

BOOST_AUTO_TEST_CASE(TestResourceRegistration) {
  // Create a custom test resource
  auto testResource = Resource::create<Resource>(
      "test_resource", "Test Resource", ResourceCategory::Item,
      ResourceType::Consumable);

  // Set additional properties
  testResource->setValue(10.0f);
  testResource->setMaxStackSize(999);
  testResource->setConsumable(true);
  testResource->setDescription("A resource for testing");

  // Register the resource
  bool registered = resourceManager->registerResourceTemplate(testResource);
  BOOST_CHECK(registered);

  // Try to retrieve it
  auto retrieved = resourceManager->getResourceTemplate("test_resource");
  BOOST_REQUIRE(retrieved != nullptr);
  BOOST_CHECK_EQUAL(retrieved->getName(), "Test Resource");
  BOOST_CHECK_EQUAL(retrieved->getDescription(), "A resource for testing");

  // Try to register the same resource again (should fail)
  bool registerAgain = resourceManager->registerResourceTemplate(testResource);
  BOOST_CHECK(!registerAgain);
}

BOOST_AUTO_TEST_CASE(TestResourceRetrieval) {
  // Test retrieving existing resource
  auto resource = resourceManager->getResourceTemplate("health_potion");
  BOOST_CHECK(resource != nullptr);

  // Test retrieving non-existent resource
  auto nonExistent =
      resourceManager->getResourceTemplate("non_existent_resource");
  BOOST_CHECK(nonExistent == nullptr);

  // Test hasResourceTemplate
  BOOST_CHECK(resourceManager->hasResourceTemplate("health_potion"));
  BOOST_CHECK(!resourceManager->hasResourceTemplate("non_existent_resource"));
}

BOOST_AUTO_TEST_CASE(TestResourcesByCategory) {
  auto itemResources =
      resourceManager->getResourcesByCategory(ResourceCategory::Item);
  BOOST_CHECK(itemResources.size() > 0);

  // Verify all returned resources are actually items
  for (const auto &resource : itemResources) {
    BOOST_CHECK_EQUAL(static_cast<int>(resource->getCategory()),
                      static_cast<int>(ResourceCategory::Item));
  }

  auto materialResources =
      resourceManager->getResourcesByCategory(ResourceCategory::Material);
  BOOST_CHECK(materialResources.size() > 0);

  for (const auto &resource : materialResources) {
    BOOST_CHECK_EQUAL(static_cast<int>(resource->getCategory()),
                      static_cast<int>(ResourceCategory::Material));
  }
}

BOOST_AUTO_TEST_CASE(TestResourcesByType) {
  auto consumableResources =
      resourceManager->getResourcesByType(ResourceType::Consumable);
  BOOST_CHECK(consumableResources.size() > 0);

  for (const auto &resource : consumableResources) {
    BOOST_CHECK_EQUAL(static_cast<int>(resource->getType()),
                      static_cast<int>(ResourceType::Consumable));
  }

  auto equipmentResources =
      resourceManager->getResourcesByType(ResourceType::Equipment);
  BOOST_CHECK(equipmentResources.size() > 0);

  for (const auto &resource : equipmentResources) {
    BOOST_CHECK_EQUAL(static_cast<int>(resource->getType()),
                      static_cast<int>(ResourceType::Equipment));
  }
}

BOOST_AUTO_TEST_CASE(TestResourceCreation) {
  // Test creating resource instances from templates
  auto healthPotionInstance = resourceManager->createResource("health_potion");
  BOOST_REQUIRE(healthPotionInstance != nullptr);
  BOOST_CHECK_EQUAL(healthPotionInstance->getName(), "Health Potion");

  // Test creating from non-existent template
  auto nonExistent = resourceManager->createResource("non_existent_resource");
  BOOST_CHECK(nonExistent == nullptr);
}

BOOST_AUTO_TEST_CASE(TestThreadSafety) {
  const int NUM_THREADS = 10;
  const int OPERATIONS_PER_THREAD = 100;
  std::atomic<int> successfulReads{0};
  std::atomic<int> successfulRegistrations{0};

  std::vector<std::thread> threads;

  // Create threads that perform concurrent operations
  for (int i = 0; i < NUM_THREADS; ++i) {
    threads.emplace_back([&, i]() {
      for (int j = 0; j < OPERATIONS_PER_THREAD; ++j) {
        // Test concurrent reads
        auto resource = resourceManager->getResourceTemplate("health_potion");
        if (resource != nullptr) {
          successfulReads++;
        }

        // Test concurrent registrations (most will fail due to duplicates,
        // which is expected)
        auto testResource = Resource::create<Resource>(
            "thread_test_" + std::to_string(i) + "_" + std::to_string(j),
            "Thread Test Resource", ResourceCategory::Item,
            ResourceType::Consumable);

        if (resourceManager->registerResourceTemplate(testResource)) {
          successfulRegistrations++;
        }

        // Small delay to increase chance of race conditions
        std::this_thread::sleep_for(std::chrono::microseconds(1));
      }
    });
  }

  // Wait for all threads to complete
  for (auto &thread : threads) {
    thread.join();
  }

  // Verify that reads were successful (should be all successful)
  BOOST_CHECK_EQUAL(successfulReads.load(),
                    NUM_THREADS * OPERATIONS_PER_THREAD);

  // Verify that some registrations were successful
  BOOST_CHECK(successfulRegistrations.load() > 0);

  // The exact number of successful registrations depends on thread timing,
  // but it should be reasonable
  BOOST_CHECK(successfulRegistrations.load() <=
              NUM_THREADS * OPERATIONS_PER_THREAD);
}

BOOST_AUTO_TEST_CASE(TestResourceValidation) {
  // Test registering null resource
  bool result = resourceManager->registerResourceTemplate(nullptr);
  BOOST_CHECK(!result);

  // Test registering resource with empty ID
  auto invalidResource = Resource::create<Resource>(
      "", // empty ID
      "Invalid Resource", ResourceCategory::Item, ResourceType::Consumable);

  result = resourceManager->registerResourceTemplate(invalidResource);
  BOOST_CHECK(!result);
}

BOOST_AUTO_TEST_CASE(TestResourceProperties) {
  auto healthPotion = resourceManager->getResourceTemplate("health_potion");
  BOOST_REQUIRE(healthPotion != nullptr);

  // Test basic properties
  BOOST_CHECK_EQUAL(healthPotion->getId(), "health_potion");
  BOOST_CHECK_EQUAL(healthPotion->getName(), "Health Potion");
  BOOST_CHECK(!healthPotion->getDescription().empty());

  // Test resource-specific properties
  BOOST_CHECK(healthPotion->getValue() >= 0);
  BOOST_CHECK(healthPotion->getMaxStackSize() > 0);
  BOOST_CHECK(healthPotion->isStackable());

  auto ironSword = resourceManager->getResourceTemplate("iron_sword");
  BOOST_REQUIRE(ironSword != nullptr);

  // Equipment typically has higher value
  BOOST_CHECK(ironSword->getValue() > 0);
}

BOOST_AUTO_TEST_CASE(TestResourceStats) {
  // Get initial stats
  ResourceStats stats = resourceManager->getStats();
  uint64_t initialTemplates = stats.templatesLoaded.load();

  // Register a new template
  auto testResource = Resource::create<Resource>(
      "stats_test_resource", "Stats Test Resource", ResourceCategory::Item,
      ResourceType::Consumable);

  bool registered = resourceManager->registerResourceTemplate(testResource);
  BOOST_REQUIRE(registered);

  // Check that stats updated
  ResourceStats newStats = resourceManager->getStats();
  BOOST_CHECK_EQUAL(newStats.templatesLoaded.load(), initialTemplates + 1);

  // Test stats reset
  resourceManager->resetStats();
  ResourceStats resetStats = resourceManager->getStats();
  BOOST_CHECK_EQUAL(resetStats.templatesLoaded.load(), 0);
  BOOST_CHECK_EQUAL(resetStats.resourcesCreated.load(), 0);
  BOOST_CHECK_EQUAL(resetStats.resourcesDestroyed.load(), 0);
}

BOOST_AUTO_TEST_CASE(TestMemoryUsage) {
  // Test memory usage reporting
  size_t memoryUsage = resourceManager->getMemoryUsage();
  BOOST_CHECK(memoryUsage >
              0); // Should have some memory usage with default resources

  // Register additional resource and check if memory usage increases
  auto testResource = Resource::create<Resource>(
      "memory_test_resource",
      "Memory Test Resource with a very long name to increase memory usage",
      ResourceCategory::Item, ResourceType::Consumable);
  testResource->setDescription(
      "This is a very long description that should increase the memory "
      "footprint of this resource for testing purposes.");

  bool registered = resourceManager->registerResourceTemplate(testResource);
  BOOST_REQUIRE(registered);

  size_t newMemoryUsage = resourceManager->getMemoryUsage();
  BOOST_CHECK(newMemoryUsage > memoryUsage);
}

BOOST_AUTO_TEST_SUITE_END()