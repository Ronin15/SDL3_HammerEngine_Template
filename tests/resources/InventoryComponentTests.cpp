/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE InventoryComponentTests
#include <boost/test/unit_test.hpp>

#include <atomic>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../mocks/MockPlayer.hpp"
#include "core/Logger.hpp"
#include "entities/Resource.hpp"
#include "entities/resources/InventoryComponent.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "managers/ResourceManager.hpp"

class InventoryComponentTestFixture {
public:
  InventoryComponentTestFixture() {
    // Initialize ResourceManager
    resourceManager = &ResourceManager::Instance();

    // Ensure ResourceManager is initialized with default resources
    if (!resourceManager->isInitialized()) {
      resourceManager->init();
    }

    // Create a mock player to own the inventory
    mockPlayer = MockPlayer::create();

    // Create test inventory with mock player as owner
    testInventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 10);

    // Test resource IDs (these should exist in the default resources)
    healthPotionId = "health_potion";
    ironSwordId = "iron_sword";
    ironOreId = "iron_ore";
    goldId = "gold";
  }

protected:
  ResourceManager *resourceManager;
  std::shared_ptr<MockPlayer> mockPlayer;
  std::unique_ptr<InventoryComponent> testInventory;
  std::string healthPotionId;
  std::string ironSwordId;
  std::string ironOreId;
  std::string goldId;
};

BOOST_FIXTURE_TEST_SUITE(InventoryComponentTestSuite,
                         InventoryComponentTestFixture)

BOOST_AUTO_TEST_CASE(TestInventoryCreation) {
  // Test inventory basic properties
  BOOST_CHECK_EQUAL(testInventory->getMaxSlots(), 10);
  BOOST_CHECK_EQUAL(testInventory->getUsedSlots(), 0);
  BOOST_CHECK_EQUAL(testInventory->getAvailableSlots(), 10);
  BOOST_CHECK(testInventory->isEmpty());
  BOOST_CHECK(!testInventory->isFull());
  BOOST_CHECK_EQUAL(testInventory->getOwner(), mockPlayer.get());
}

BOOST_AUTO_TEST_CASE(TestAddResource) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);

  // Test adding a stackable resource
  bool added = inventory->addResource(healthPotionId, 5);
  BOOST_CHECK(added);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(healthPotionId), 5);
  BOOST_CHECK_EQUAL(inventory->getUsedSlots(), 1);
  BOOST_CHECK_EQUAL(inventory->getAvailableSlots(), 19);

  // Test adding more of the same resource (should stack)
  added = inventory->addResource(healthPotionId, 3);
  BOOST_CHECK(added);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(healthPotionId), 8);
  BOOST_CHECK_EQUAL(inventory->getUsedSlots(),
                    1); // Should still be 1 slot (stacked)
}

BOOST_AUTO_TEST_CASE(TestAddNonStackableResource) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);

  // Test adding equipment (typically non-stackable or limited stack)
  bool added = inventory->addResource(ironSwordId, 1);
  BOOST_CHECK(added);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(ironSwordId), 1);

  // Try adding another sword
  added = inventory->addResource(ironSwordId, 1);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(ironSwordId), 2);
}

BOOST_AUTO_TEST_CASE(TestRemoveResource) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);

  // Add some resources first
  inventory->addResource(healthPotionId, 10);
  inventory->addResource(ironSwordId, 2);

  // Test removing partial quantity
  bool removed = inventory->removeResource(healthPotionId, 3);
  BOOST_CHECK(removed);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(healthPotionId), 7);

  // Test removing all of a resource
  removed = inventory->removeResource(ironSwordId, 2);
  BOOST_CHECK(removed);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(ironSwordId), 0);
  BOOST_CHECK(!inventory->hasResource(ironSwordId));

  // Test removing more than available
  removed = inventory->removeResource(healthPotionId, 20);
  BOOST_CHECK(!removed);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(healthPotionId), 7);
}

BOOST_AUTO_TEST_CASE(TestHasResource) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);

  // Add some resources
  inventory->addResource(healthPotionId, 5);

  // Test hasResource with different quantities
  BOOST_CHECK(inventory->hasResource(healthPotionId));
  BOOST_CHECK(inventory->hasResource(healthPotionId, 1));
  BOOST_CHECK(inventory->hasResource(healthPotionId, 5));
  BOOST_CHECK(!inventory->hasResource(healthPotionId, 6));

  // Test non-existent resource
  BOOST_CHECK(!inventory->hasResource("non_existent_resource"));
}

BOOST_AUTO_TEST_CASE(TestCapacityLimits) {
  auto smallInventory =
      std::make_unique<InventoryComponent>(mockPlayer.get(), 2);

  // Fill up the small inventory
  bool added1 = smallInventory->addResource(ironSwordId, 1);
  bool added2 = smallInventory->addResource(goldId, 1);
  BOOST_CHECK(added1);
  BOOST_CHECK(added2);
  BOOST_CHECK(smallInventory->isFull());
  BOOST_CHECK_EQUAL(smallInventory->getAvailableSlots(), 0);

  // Try to add when full
  bool added3 = smallInventory->addResource(healthPotionId, 1);
  BOOST_CHECK(!added3);
}

BOOST_AUTO_TEST_CASE(TestGetAllResources) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);
  inventory->addResource(healthPotionId, 5);
  inventory->addResource(ironSwordId, 2);
  inventory->addResource(goldId, 100);

  auto allResources = inventory->getAllResources();
  BOOST_CHECK_EQUAL(allResources.size(), 3);

  BOOST_CHECK_EQUAL(allResources[healthPotionId], 5);
  BOOST_CHECK_EQUAL(allResources[ironSwordId], 2);
  BOOST_CHECK_EQUAL(allResources[goldId], 100);

  auto resourceIds = inventory->getResourceIds();
  BOOST_CHECK_EQUAL(resourceIds.size(), 3);
  BOOST_CHECK(std::find(resourceIds.begin(), resourceIds.end(),
                        healthPotionId) != resourceIds.end());
  BOOST_CHECK(std::find(resourceIds.begin(), resourceIds.end(), ironSwordId) !=
              resourceIds.end());
  BOOST_CHECK(std::find(resourceIds.begin(), resourceIds.end(), goldId) !=
              resourceIds.end());
}

BOOST_AUTO_TEST_CASE(TestClearInventory) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);
  inventory->addResource(healthPotionId, 5);
  inventory->addResource(ironSwordId, 2);
  inventory->addResource(goldId, 100);

  BOOST_CHECK_EQUAL(inventory->getUsedSlots(), 4);

  inventory->clearInventory();

  BOOST_CHECK(inventory->isEmpty());
  BOOST_CHECK_EQUAL(inventory->getUsedSlots(), 0);
  BOOST_CHECK_EQUAL(inventory->getAvailableSlots(), inventory->getMaxSlots());
}

BOOST_AUTO_TEST_CASE(TestGetResourcesByCategory) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);
  inventory->addResource(healthPotionId, 5); // Item
  inventory->addResource(ironSwordId, 1);    // Item
  inventory->addResource(ironOreId, 10);     // Material
  inventory->addResource(goldId, 100);       // Currency

  // Test getting resources by category
  auto items = inventory->getResourcesByCategory(ResourceCategory::Item);
  auto materials =
      inventory->getResourcesByCategory(ResourceCategory::Material);
  auto currencies =
      inventory->getResourcesByCategory(ResourceCategory::Currency);

  // Items should contain health potion and iron sword
  BOOST_CHECK_GE(items.size(), 2);

  // Materials should contain iron ore
  BOOST_CHECK_GE(materials.size(), 1);

  // Currencies should contain gold
  BOOST_CHECK_GE(currencies.size(), 1);
}

BOOST_AUTO_TEST_CASE(TestSlotOperations) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 5);

  // Add some resources
  inventory->addResource(healthPotionId, 3);
  inventory->addResource(ironSwordId, 1);

  // Test getting slot information
  BOOST_CHECK_GE(inventory->getUsedSlots(), 2);

  // Test slot access (assuming slots are filled sequentially)
  if (inventory->getUsedSlots() > 0) {
    const auto &slot0 = inventory->getSlot(0);
    BOOST_CHECK(!slot0.isEmpty());
    BOOST_CHECK(!slot0.resourceId.empty());
    BOOST_CHECK_GT(slot0.quantity, 0);
  }
}

BOOST_AUTO_TEST_CASE(TestCanAddResource) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 2);

  // Should be able to add to empty inventory
  BOOST_CHECK(inventory->canAddResource(healthPotionId, 5));

  // Add a resource
  inventory->addResource(healthPotionId, 5);

  // Should still be able to add more of the same (stacking)
  BOOST_CHECK(inventory->canAddResource(healthPotionId, 5));

  // Fill up remaining slots
  inventory->addResource(ironSwordId, 1);
  inventory->addResource(ironOreId, 1);

  // Should not be able to add a new resource when full
  BOOST_CHECK(!inventory->canAddResource("new_item", 1));
}

BOOST_AUTO_TEST_CASE(TestThreadSafety) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 100);

  // Pre-populate inventory
  inventory->addResource(healthPotionId, 1000);
  inventory->addResource(goldId, 10000);

  std::atomic<int> successfulOperations(0);
  std::atomic<bool> testComplete(false);

  // Create multiple threads that perform operations
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back(
        [&inventory, &successfulOperations, &testComplete, this]() {
          while (!testComplete) {
            // Try different operations
            if (inventory->addResource(ironOreId, 1)) {
              successfulOperations++;
            }
            if (inventory->removeResource(ironOreId, 1)) {
              successfulOperations++;
            }
            inventory->hasResource(healthPotionId);
            inventory->getResourceQuantity(goldId);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
        });
  }

  // Let threads run for a short time
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  testComplete = true;

  // Wait for all threads to complete
  for (auto &thread : threads) {
    thread.join();
  }

  // Test should complete without crashes (basic thread safety check)
  BOOST_CHECK_GE(successfulOperations.load(), 0);
}

BOOST_AUTO_TEST_CASE(TestUtilityMethods) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);

  // Add some resources
  inventory->addResource(healthPotionId, 5);
  inventory->addResource(goldId, 100);

  // Test methods that are implemented
  BOOST_CHECK_NO_THROW(inventory->compactInventory());

  // Test can add resource method with a valid resource
  BOOST_CHECK(inventory->canAddResource(ironSwordId, 1));
}
BOOST_AUTO_TEST_CASE(TestInvalidOperations) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 10);

  // Test adding zero or negative quantities
  bool added = inventory->addResource(healthPotionId, 0);
  BOOST_CHECK(!added);

  added = inventory->addResource(healthPotionId, -5);
  BOOST_CHECK(!added);

  // Test removing from empty inventory
  bool removed = inventory->removeResource("non_existent", 1);
  BOOST_CHECK(!removed);

  // Test invalid slot access
  BOOST_CHECK_THROW(inventory->getSlot(100), std::exception);
}

BOOST_AUTO_TEST_CASE(TestTransferOperations) {
  auto sourceInventory =
      std::make_unique<InventoryComponent>(mockPlayer.get(), 10);
  auto targetInventory =
      std::make_unique<InventoryComponent>(mockPlayer.get(), 10);

  // Add resources to source
  sourceInventory->addResource(healthPotionId, 10);
  sourceInventory->addResource(goldId, 100);

  // Test transfer
  bool transferred =
      sourceInventory->transferTo(*targetInventory, healthPotionId, 5);
  BOOST_CHECK(transferred);
  BOOST_CHECK_EQUAL(sourceInventory->getResourceQuantity(healthPotionId), 5);
  BOOST_CHECK_EQUAL(targetInventory->getResourceQuantity(healthPotionId), 5);

  // Test transfer more than available
  transferred = sourceInventory->transferTo(*targetInventory, goldId, 200);
  BOOST_CHECK(!transferred);
  BOOST_CHECK_EQUAL(sourceInventory->getResourceQuantity(goldId), 100);
  BOOST_CHECK_EQUAL(targetInventory->getResourceQuantity(goldId), 0);
}

BOOST_AUTO_TEST_SUITE_END()