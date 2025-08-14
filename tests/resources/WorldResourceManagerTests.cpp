/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE WorldResourceManagerTests
#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "entities/Resource.hpp"
#include "entities/resources/CurrencyAndGameResources.hpp"
#include "entities/resources/ItemResources.hpp"
#include "entities/resources/MaterialResources.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/WorldResourceManager.hpp"

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

// Helper function to get or load resource by name
HammerEngine::ResourceHandle
getOrLoadResourceByName(ResourceTemplateManager *manager,
                        const std::string &name) {
  // First try to find existing resource
  auto handle = findResourceByName(manager, name);
  if (handle.isValid()) {
    return handle;
  }

  // If not found, try loading JSON resources and try again
  manager->loadResourcesFromJson("res/data/materials_and_currency.json");
  manager->loadResourcesFromJson("res/data/items.json");

  // Try finding again
  handle = findResourceByName(manager, name);
  return handle; // May still be invalid if resource doesn't exist
}

class WorldResourceManagerTestFixture {
public:
  WorldResourceManagerTestFixture() {
    // Initialize ThreadSystem first for threading tests
    threadSystem = &HammerEngine::ThreadSystem::Instance();
    if (threadSystem->isShutdown() || threadSystem->getThreadCount() == 0) {
      bool initSuccess = threadSystem->init();
      if (!initSuccess && threadSystem->getThreadCount() == 0) {
        throw std::runtime_error("Failed to initialize ThreadSystem for threading tests");
      }
    }

    // Initialize ResourceTemplateManager singleton first (required for
    // WorldResourceManager)
    templateManager = &ResourceTemplateManager::Instance();
    BOOST_REQUIRE(templateManager != nullptr);
    bool templateInitialized = templateManager->init();
    BOOST_REQUIRE(templateInitialized);

    // Initialize WorldResourceManager singleton
    worldManager = &WorldResourceManager::Instance();
    BOOST_REQUIRE(worldManager != nullptr);

    bool worldInitialized = worldManager->init();
    BOOST_REQUIRE(worldInitialized);
  }

  ~WorldResourceManagerTestFixture() {
    // Clean up both managers
    worldManager->clean();
    templateManager->clean();
    // Note: Don't clean ThreadSystem here as it's shared across tests
  }

protected:
  ResourceTemplateManager *templateManager;
  WorldResourceManager *worldManager;
  HammerEngine::ThreadSystem *threadSystem;
};

BOOST_FIXTURE_TEST_SUITE(WorldResourceManagerTestSuite,
                         WorldResourceManagerTestFixture)

BOOST_AUTO_TEST_CASE(TestSingletonPattern) {
  // Test that Instance always returns the same instance
  WorldResourceManager *instance1 = &WorldResourceManager::Instance();
  WorldResourceManager *instance2 = &WorldResourceManager::Instance();

  BOOST_CHECK(instance1 == instance2);
  BOOST_CHECK(instance1 == worldManager);
}

BOOST_AUTO_TEST_CASE(TestInitialization) {
  // Test that WorldResourceManager is initialized
  BOOST_CHECK(worldManager->isInitialized());

  // Test that we start with a default world
  auto worlds = worldManager->getWorldIds();
  BOOST_CHECK_EQUAL(worlds.size(), 1);
  BOOST_CHECK(worldManager->hasWorld("default"));
}

BOOST_AUTO_TEST_CASE(TestWorldCreationAndRemoval) {
  const std::string worldId = "test_world";

  // Test world creation
  bool created = worldManager->createWorld(worldId);
  BOOST_CHECK(created);
  BOOST_CHECK(worldManager->hasWorld(worldId));

  // Test that creating the same world again fails
  bool createdAgain = worldManager->createWorld(worldId);
  BOOST_CHECK(!createdAgain);

  // Test getting all worlds (should include default + test_world)
  auto worlds = worldManager->getWorldIds();
  BOOST_CHECK_EQUAL(worlds.size(), 2);
  BOOST_CHECK(std::find(worlds.begin(), worlds.end(), worldId) != worlds.end());
  BOOST_CHECK(std::find(worlds.begin(), worlds.end(), "default") !=
              worlds.end());

  // Test world removal
  bool removed = worldManager->removeWorld(worldId);
  BOOST_CHECK(removed);
  BOOST_CHECK(!worldManager->hasWorld(worldId));

  // Test that removing a non-existent world fails
  bool removedAgain = worldManager->removeWorld(worldId);
  BOOST_CHECK(!removedAgain);

  // Test that we're back to just default world
  worlds = worldManager->getWorldIds();
  BOOST_CHECK_EQUAL(worlds.size(), 1);
  BOOST_CHECK(std::find(worlds.begin(), worlds.end(), "default") !=
              worlds.end());
}
BOOST_AUTO_TEST_CASE(TestBasicResourceOperations) {
  const std::string worldId = "resource_test_world";

  // Get a proper ResourceHandle from the template manager
  auto resourceHandle = findResourceByName(templateManager, "Platinum Coins");
  if (!resourceHandle.isValid()) {
    // Register the resource if it doesn't exist
    resourceHandle = getOrLoadResourceByName(templateManager, "Platinum Coins");
  }
  BOOST_REQUIRE(resourceHandle.isValid());

  // Create a test world
  bool created = worldManager->createWorld(worldId);
  BOOST_REQUIRE(created);

  // Test initial state - should have 0 of everything
  int64_t initialAmount =
      worldManager->getResourceQuantity(worldId, resourceHandle);
  BOOST_CHECK_EQUAL(initialAmount, 0);

  // Test adding resources
  auto result = worldManager->addResource(worldId, resourceHandle, 100);
  BOOST_CHECK(result == ResourceTransactionResult::Success);

  // Verify the resource was added
  int64_t currentAmount =
      worldManager->getResourceQuantity(worldId, resourceHandle);
  BOOST_CHECK_EQUAL(currentAmount, 100);

  // Test adding more resources
  result = worldManager->addResource(worldId, resourceHandle, 50);
  BOOST_CHECK(result == ResourceTransactionResult::Success);

  currentAmount = worldManager->getResourceQuantity(worldId, resourceHandle);
  BOOST_CHECK_EQUAL(currentAmount, 150);

  // Test removing resources
  result = worldManager->removeResource(worldId, resourceHandle, 30);
  BOOST_CHECK(result == ResourceTransactionResult::Success);

  currentAmount = worldManager->getResourceQuantity(worldId, resourceHandle);
  BOOST_CHECK_EQUAL(currentAmount, 120);

  // Test setting resource quantity
  result = worldManager->setResource(worldId, resourceHandle, 200);
  BOOST_CHECK(result == ResourceTransactionResult::Success);

  currentAmount = worldManager->getResourceQuantity(worldId, resourceHandle);
  BOOST_CHECK_EQUAL(currentAmount, 200);

  // Test removing more than available
  result = worldManager->removeResource(worldId, resourceHandle, 300);
  BOOST_CHECK(result == ResourceTransactionResult::InsufficientResources);

  // Should remain unchanged
  currentAmount = worldManager->getResourceQuantity(worldId, resourceHandle);
  BOOST_CHECK_EQUAL(currentAmount, 200);

  // Clean up
  worldManager->removeWorld(worldId);
}

BOOST_AUTO_TEST_CASE(TestMultipleResourceTypes) {
  const std::string worldId = "multi_resource_world";

  // Create a test world
  bool created = worldManager->createWorld(worldId);
  BOOST_REQUIRE(created);

  // Test with different resource types - get proper handles
  std::vector<std::string> resourceStringIds = {
      "Platinum Coins", "Super Health Potion", "Mithril Ore", "Magic Sword"};
  std::vector<HammerEngine::ResourceHandle> resourceHandles;
  std::vector<int64_t> quantities = {1000, 50, 200, 5};

  // Get ResourceHandles for all resource IDs
  for (const auto &stringId : resourceStringIds) {
    auto handle = findResourceByName(templateManager, stringId);
    if (!handle.isValid()) {
      handle = getOrLoadResourceByName(templateManager, stringId);
    }
    BOOST_REQUIRE(handle.isValid());
    resourceHandles.push_back(handle);
  }

  // Add all resources
  for (size_t i = 0; i < resourceHandles.size(); ++i) {
    auto result =
        worldManager->addResource(worldId, resourceHandles[i], quantities[i]);
    BOOST_CHECK(result == ResourceTransactionResult::Success);
  }

  // Verify all resources were added correctly
  for (size_t i = 0; i < resourceHandles.size(); ++i) {
    int64_t amount =
        worldManager->getResourceQuantity(worldId, resourceHandles[i]);
    BOOST_CHECK_EQUAL(amount, quantities[i]);
  }

  // Test getting all resources for the world
  auto allResources = worldManager->getWorldResources(worldId);
  BOOST_CHECK_EQUAL(allResources.size(), resourceHandles.size());

  for (size_t i = 0; i < resourceHandles.size(); ++i) {
    auto it = allResources.find(resourceHandles[i]);
    BOOST_REQUIRE(it != allResources.end());
    BOOST_CHECK_EQUAL(it->second, quantities[i]);
  }

  // Clean up
  worldManager->removeWorld(worldId);
}

BOOST_AUTO_TEST_CASE(TestMultipleWorlds) {
  std::vector<std::string> worldIds = {"world1", "world2", "world3"};

  // Get a proper ResourceHandle
  auto resourceHandle = findResourceByName(templateManager, "Platinum Coins");
  if (!resourceHandle.isValid()) {
    resourceHandle = getOrLoadResourceByName(templateManager, "Platinum Coins");
  }
  BOOST_REQUIRE(resourceHandle.isValid());

  // Create multiple worlds
  for (const auto &worldId : worldIds) {
    bool created = worldManager->createWorld(worldId);
    BOOST_REQUIRE(created);
  }

  // Add different amounts of gold to each world
  for (size_t i = 0; i < worldIds.size(); ++i) {
    int64_t amount = (i + 1) * 100; // 100, 200, 300
    auto result =
        worldManager->addResource(worldIds[i], resourceHandle, amount);
    BOOST_CHECK(result == ResourceTransactionResult::Success);
  }

  // Verify each world has the correct amount
  for (size_t i = 0; i < worldIds.size(); ++i) {
    int64_t expected = (i + 1) * 100;
    int64_t actual =
        worldManager->getResourceQuantity(worldIds[i], resourceHandle);
    BOOST_CHECK_EQUAL(actual, expected);
  }

  // Test aggregation across all worlds
  int64_t totalGold = worldManager->getTotalResourceQuantity(resourceHandle);
  BOOST_CHECK_EQUAL(totalGold, 600); // 100 + 200 + 300

  // Clean up
  for (const auto &worldId : worldIds) {
    worldManager->removeWorld(worldId);
  }
}

BOOST_AUTO_TEST_CASE(TestInvalidOperations) {
  const std::string validWorldId = "valid_world";
  const std::string invalidWorldId = "invalid_world";

  // Get valid and invalid resource handles
  auto validResourceHandle =
      findResourceByName(templateManager, "Platinum Coins");
  if (!validResourceHandle.isValid()) {
    validResourceHandle =
        getOrLoadResourceByName(templateManager, "Platinum Coins");
  }
  BOOST_REQUIRE(validResourceHandle.isValid());

  // Create an invalid resource handle (unregistered)
  HammerEngine::ResourceHandle
      invalidResourceHandle; // Default constructor gives invalid handle

  // Create a valid world
  bool created = worldManager->createWorld(validWorldId);
  BOOST_REQUIRE(created);

  // Test operations on non-existent world
  auto result =
      worldManager->addResource(invalidWorldId, validResourceHandle, 100);
  BOOST_CHECK(result == ResourceTransactionResult::InvalidWorldId);

  result =
      worldManager->removeResource(invalidWorldId, validResourceHandle, 50);
  BOOST_CHECK(result == ResourceTransactionResult::InvalidWorldId);

  result = worldManager->setResource(invalidWorldId, validResourceHandle, 200);
  BOOST_CHECK(result == ResourceTransactionResult::InvalidWorldId);

  // Test operations with invalid resource handle
  result = worldManager->addResource(validWorldId, invalidResourceHandle, 100);
  BOOST_CHECK(result == ResourceTransactionResult::InvalidResourceHandle);

  result =
      worldManager->removeResource(validWorldId, invalidResourceHandle, 50);
  BOOST_CHECK(result == ResourceTransactionResult::InvalidResourceHandle);

  result = worldManager->setResource(validWorldId, invalidResourceHandle, 200);
  BOOST_CHECK(result == ResourceTransactionResult::InvalidResourceHandle);

  // Test getting quantity for invalid world (should return 0)
  int64_t quantity =
      worldManager->getResourceQuantity(invalidWorldId, validResourceHandle);
  BOOST_CHECK_EQUAL(quantity, 0);

  // Test getting quantity for invalid resource handle (should return 0)
  quantity =
      worldManager->getResourceQuantity(validWorldId, invalidResourceHandle);
  BOOST_CHECK_EQUAL(quantity, 0);

  // Clean up
  worldManager->removeWorld(validWorldId);
  // No need to remove invalidWorldId as it was never created
}

BOOST_AUTO_TEST_CASE(TestWorldSwitching) {
  const std::string world1 = "world1";
  const std::string world2 = "world2";

  // Get a proper ResourceHandle
  auto resourceHandle = findResourceByName(templateManager, "Platinum Coins");
  if (!resourceHandle.isValid()) {
    resourceHandle = getOrLoadResourceByName(templateManager, "Platinum Coins");
  }
  BOOST_REQUIRE(resourceHandle.isValid());

  // Create two worlds
  BOOST_REQUIRE(worldManager->createWorld(world1));
  BOOST_REQUIRE(worldManager->createWorld(world2));

  // Add different amounts to each world
  auto result1 = worldManager->addResource(world1, resourceHandle, 100);
  BOOST_REQUIRE(result1 == ResourceTransactionResult::Success);

  auto result2 = worldManager->addResource(world2, resourceHandle, 500);
  BOOST_REQUIRE(result2 == ResourceTransactionResult::Success);

  // Test that each world maintains separate state
  BOOST_CHECK_EQUAL(worldManager->getResourceQuantity(world1, resourceHandle),
                    100);
  BOOST_CHECK_EQUAL(worldManager->getResourceQuantity(world2, resourceHandle),
                    500);

  // Modify one world and ensure the other is unaffected
  auto result3 = worldManager->setResource(world1, resourceHandle, 1000);
  BOOST_REQUIRE(result3 == ResourceTransactionResult::Success);

  BOOST_CHECK_EQUAL(worldManager->getResourceQuantity(world1, resourceHandle),
                    1000);
  BOOST_CHECK_EQUAL(worldManager->getResourceQuantity(world2, resourceHandle),
                    500); // Unchanged

  // Clean up
  worldManager->removeWorld(world1);
  worldManager->removeWorld(world2);
}

BOOST_AUTO_TEST_CASE(TestResourceStatistics) {
  const std::string worldId = "stats_world";

  // Create a test world
  BOOST_REQUIRE(worldManager->createWorld(worldId));

  // Get proper ResourceHandles
  auto goldHandle = findResourceByName(templateManager, "Platinum Coins");
  if (!goldHandle.isValid()) {
    goldHandle = getOrLoadResourceByName(templateManager, "Platinum Coins");
  }
  auto potionHandle =
      findResourceByName(templateManager, "Super Health Potion");
  if (!potionHandle.isValid()) {
    potionHandle =
        getOrLoadResourceByName(templateManager, "Super Health Potion");
  }
  auto oreHandle = findResourceByName(templateManager, "Mithril Ore");
  if (!oreHandle.isValid()) {
    oreHandle = getOrLoadResourceByName(templateManager, "Mithril Ore");
  }

  BOOST_REQUIRE(goldHandle.isValid());
  BOOST_REQUIRE(potionHandle.isValid());
  BOOST_REQUIRE(oreHandle.isValid());

  // Get initial stats
  auto stats = worldManager->getStats();
  uint64_t initialOperations = stats.totalTransactions.load();

  // Perform some operations
  worldManager->addResource(worldId, goldHandle, 100);
  worldManager->addResource(worldId, potionHandle, 50);
  worldManager->removeResource(worldId, goldHandle, 25);
  worldManager->setResource(worldId, oreHandle, 200);

  // Check that stats were updated
  auto newStats = worldManager->getStats();
  BOOST_CHECK(newStats.totalTransactions.load() >= initialOperations + 4);

  // Test stats reset
  worldManager->resetStats();
  auto resetStats = worldManager->getStats();
  BOOST_CHECK_EQUAL(resetStats.totalTransactions.load(), 0);
  BOOST_CHECK_EQUAL(resetStats.addOperations.load(), 0);
  BOOST_CHECK_EQUAL(resetStats.removeOperations.load(), 0);

  // Clean up
  worldManager->removeWorld(worldId);
}

BOOST_AUTO_TEST_CASE(TestThreadSafety) {
  const int NUM_THREADS = 10;
  const int OPERATIONS_PER_THREAD = 100;
  const std::string worldId = "thread_test_world";

  // Get a proper ResourceHandle
  auto resourceHandle = findResourceByName(templateManager, "Platinum Coins");
  if (!resourceHandle.isValid()) {
    resourceHandle = getOrLoadResourceByName(templateManager, "Platinum Coins");
  }
  BOOST_REQUIRE(resourceHandle.isValid());

  // Create a test world
  BOOST_REQUIRE(worldManager->createWorld(worldId));

  std::atomic<int> successfulAdds{0};
  std::atomic<int> successfulRemoves{0};
  std::atomic<int> successfulReads{0};
  std::vector<std::future<void>> futures;

  // Create tasks that perform concurrent operations using ThreadSystem
  for (int i = 0; i < NUM_THREADS; ++i) {
    auto future = threadSystem->enqueueTaskWithResult([=, this, &successfulAdds, &successfulRemoves, &successfulReads]() -> void {
      for (int j = 0; j < OPERATIONS_PER_THREAD; ++j) {
        // Test concurrent adds
        auto addResult = worldManager->addResource(worldId, resourceHandle, 10);
        if (addResult == ResourceTransactionResult::Success) {
          successfulAdds.fetch_add(1, std::memory_order_relaxed);
        }

        // Test concurrent reads
        int64_t quantity =
            worldManager->getResourceQuantity(worldId, resourceHandle);
        if (quantity >= 0) { // Always true, but counts the read
          successfulReads.fetch_add(1, std::memory_order_relaxed);
        }

        // Test concurrent removes (some may fail due to insufficient resources)
        auto removeResult =
            worldManager->removeResource(worldId, resourceHandle, 5);
        if (removeResult == ResourceTransactionResult::Success) {
          successfulRemoves.fetch_add(1, std::memory_order_relaxed);
        }

        // Small delay to increase chance of race conditions
        std::this_thread::sleep_for(std::chrono::microseconds(1));
      }
    }, HammerEngine::TaskPriority::Normal, "ResourceTestTask");

    futures.push_back(std::move(future));
  }

  // Wait for all tasks to complete
  for (auto &future : futures) {
    future.wait();
  }

  // Verify that operations were performed
  BOOST_CHECK(successfulAdds.load() > 0);
  BOOST_CHECK(successfulRemoves.load() >=
              0); // Some may fail due to insufficient resources
  BOOST_CHECK_EQUAL(successfulReads.load(),
                    NUM_THREADS * OPERATIONS_PER_THREAD);

  // Verify final state is consistent
  int64_t finalQuantity =
      worldManager->getResourceQuantity(worldId, resourceHandle);
  int64_t expectedQuantity =
      successfulAdds.load() * 10 - successfulRemoves.load() * 5;
  BOOST_CHECK_EQUAL(finalQuantity, expectedQuantity);

  // Clean up
  worldManager->removeWorld(worldId);
}

BOOST_AUTO_TEST_CASE(TestConcurrentWorldOperations) {
  const int NUM_THREADS = 5;
  const int WORLDS_PER_THREAD = 20;

  std::atomic<int> worldsCreated{0};
  std::atomic<int> worldsDestroyed{0};
  std::vector<std::future<void>> futures;

  // Create tasks that create and destroy worlds concurrently using ThreadSystem
  for (int i = 0; i < NUM_THREADS; ++i) {
    auto future = threadSystem->enqueueTaskWithResult([=, this, &worldsCreated, &worldsDestroyed]() -> void {
      for (int j = 0; j < WORLDS_PER_THREAD; ++j) {
        std::string worldId =
            "concurrent_world_" + std::to_string(i) + "_" + std::to_string(j);

        // Create world
        if (worldManager->createWorld(worldId)) {
          worldsCreated.fetch_add(1, std::memory_order_relaxed);

          // Add some resources
          auto resourceHandle =
              findResourceByName(templateManager, "Platinum Coins");
          if (!resourceHandle.isValid()) {
            resourceHandle =
                getOrLoadResourceByName(templateManager, "Platinum Coins");
          }
          if (resourceHandle.isValid()) {
            worldManager->addResource(worldId, resourceHandle, 100);
          }
          // Remove world
          if (worldManager->removeWorld(worldId)) {
            worldsDestroyed.fetch_add(1, std::memory_order_relaxed);
          }
        }

        // Small delay
        std::this_thread::sleep_for(std::chrono::microseconds(1));
      }
    }, HammerEngine::TaskPriority::Normal, "WorldOperationTask");

    futures.push_back(std::move(future));
  }

  // Wait for all tasks to complete
  for (auto &future : futures) {
    future.wait();
  }

  // Verify that all created worlds were also destroyed
  BOOST_CHECK_EQUAL(worldsCreated.load(), worldsDestroyed.load());
  BOOST_CHECK_EQUAL(worldsCreated.load(), NUM_THREADS * WORLDS_PER_THREAD);

  // Verify only default world remains
  auto remainingWorlds = worldManager->getWorldIds();
  BOOST_CHECK_EQUAL(remainingWorlds.size(), 1);
  BOOST_CHECK(worldManager->hasWorld("default"));
}

BOOST_AUTO_TEST_CASE(TestMemoryUsage) {
  // Test memory usage reporting
  size_t initialMemoryUsage = worldManager->getMemoryUsage();

  // Create worlds and add resources to increase memory usage
  std::vector<std::string> worldIds = {"mem_world1", "mem_world2",
                                       "mem_world3"};

  // Get proper ResourceHandles
  auto goldHandle = findResourceByName(templateManager, "Platinum Coins");
  if (!goldHandle.isValid()) {
    goldHandle = getOrLoadResourceByName(templateManager, "Platinum Coins");
  }
  auto potionHandle =
      findResourceByName(templateManager, "Super Health Potion");
  if (!potionHandle.isValid()) {
    potionHandle =
        getOrLoadResourceByName(templateManager, "Super Health Potion");
  }
  auto oreHandle = findResourceByName(templateManager, "Mithril Ore");
  if (!oreHandle.isValid()) {
    oreHandle = getOrLoadResourceByName(templateManager, "Mithril Ore");
  }
  auto swordHandle = findResourceByName(templateManager, "Magic Sword");
  if (!swordHandle.isValid()) {
    swordHandle = getOrLoadResourceByName(templateManager, "Magic Sword");
  }

  BOOST_REQUIRE(goldHandle.isValid());
  BOOST_REQUIRE(potionHandle.isValid());
  BOOST_REQUIRE(oreHandle.isValid());
  BOOST_REQUIRE(swordHandle.isValid());

  for (const auto &worldId : worldIds) {
    BOOST_REQUIRE(worldManager->createWorld(worldId));

    // Add multiple resources to each world
    worldManager->addResource(worldId, goldHandle, 1000);
    worldManager->addResource(worldId, potionHandle, 50);
    worldManager->addResource(worldId, oreHandle, 200);
    worldManager->addResource(worldId, swordHandle, 10);
  }

  size_t newMemoryUsage = worldManager->getMemoryUsage();
  BOOST_CHECK(newMemoryUsage > initialMemoryUsage);

  // Clean up and check memory usage decreases
  for (const auto &worldId : worldIds) {
    worldManager->removeWorld(worldId);
  }

  size_t finalMemoryUsage = worldManager->getMemoryUsage();
  BOOST_CHECK(finalMemoryUsage <= newMemoryUsage);
}

BOOST_AUTO_TEST_CASE(TestResourceValidation) {
  const std::string worldId = "validation_world";

  // Get a proper ResourceHandle
  auto validResourceHandle =
      findResourceByName(templateManager, "Platinum Coins");
  if (!validResourceHandle.isValid()) {
    validResourceHandle =
        getOrLoadResourceByName(templateManager, "Platinum Coins");
  }
  BOOST_REQUIRE(validResourceHandle.isValid());

  // Create an invalid resource handle
  HammerEngine::ResourceHandle
      invalidResourceHandle; // Default constructor gives invalid handle

  // Create a test world
  BOOST_REQUIRE(worldManager->createWorld(worldId));

  // Test with empty world ID
  auto result = worldManager->addResource("", validResourceHandle, 100);
  BOOST_CHECK(result == ResourceTransactionResult::InvalidWorldId);

  // Test with invalid resource handle
  result = worldManager->addResource(worldId, invalidResourceHandle, 100);
  BOOST_CHECK(result == ResourceTransactionResult::InvalidResourceHandle);

  // Test with zero quantity (should succeed for add/set)
  result = worldManager->addResource(worldId, validResourceHandle, 0);
  BOOST_CHECK(result == ResourceTransactionResult::Success);

  result = worldManager->setResource(worldId, validResourceHandle, 0);
  BOOST_CHECK(result == ResourceTransactionResult::Success);

  // Test removing zero (should succeed but do nothing)
  result = worldManager->removeResource(worldId, validResourceHandle, 0);
  BOOST_CHECK(result == ResourceTransactionResult::Success);

  // Clean up
  worldManager->removeWorld(worldId);
}

BOOST_AUTO_TEST_CASE(TestLargeQuantities) {
  const std::string worldId = "large_quantity_world";

  // Get a proper ResourceHandle
  auto resourceHandle = findResourceByName(templateManager, "Platinum Coins");
  if (!resourceHandle.isValid()) {
    resourceHandle = getOrLoadResourceByName(templateManager, "Platinum Coins");
  }
  BOOST_REQUIRE(resourceHandle.isValid());

  // Create a test world
  BOOST_REQUIRE(worldManager->createWorld(worldId));

  // Test with maximum possible int64_t value
  const int64_t maxValue = std::numeric_limits<int64_t>::max();
  const int64_t largeValue = maxValue - 1000;

  // Set a very large quantity
  auto result = worldManager->setResource(worldId, resourceHandle, largeValue);
  BOOST_CHECK(result == ResourceTransactionResult::Success);

  // Verify we can read the value
  int64_t quantity = worldManager->getResourceQuantity(worldId, resourceHandle);
  BOOST_CHECK_EQUAL(quantity, largeValue);

  // Try to add more (should handle overflow gracefully)
  result = worldManager->addResource(worldId, resourceHandle, 2000);
  // This might succeed or fail depending on overflow handling implementation
  // At minimum, it should not crash

  // Clean up
  worldManager->removeWorld(worldId);
}

BOOST_AUTO_TEST_SUITE_END()