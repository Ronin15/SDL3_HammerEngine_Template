/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE InventoryComponentTests
#include <boost/test/unit_test.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../mocks/MockPlayer.hpp"
#include "core/Logger.hpp"
#include "entities/Resource.hpp"
#include "entities/resources/InventoryComponent.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "managers/ResourceTemplateManager.hpp"

class InventoryComponentTestFixture {
public:
  InventoryComponentTestFixture() {
    // Initialize ResourceTemplateManager
    resourceManager = &ResourceTemplateManager::Instance();

    // Ensure ResourceTemplateManager is initialized with default resources
    if (!resourceManager->isInitialized()) {
      resourceManager->init();
    }

    // Create a mock player to own the inventory
    mockPlayer = MockPlayer::create();

    // Create test inventory with mock player as owner
    testInventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 10);

    // Get actual resource handles by name (more reliable than hard-coding)
    healthPotionHandle =
        resourceManager->getHandleByName("Super Health Potion");
    swordHandle = resourceManager->getHandleByName("Magic Sword");
    oreHandle = resourceManager->getHandleByName("Mithril Ore");
    coinsHandle = resourceManager->getHandleByName("Platinum Coins");

    // Verify we got valid handles
    BOOST_REQUIRE(healthPotionHandle.isValid());
    BOOST_REQUIRE(swordHandle.isValid());
    BOOST_REQUIRE(oreHandle.isValid());
    BOOST_REQUIRE(coinsHandle.isValid());
  }

  ~InventoryComponentTestFixture() {
    // Explicitly clean up before singletons are destroyed to prevent crashes
    testInventory.reset();
    mockPlayer.reset();

    // Clean up the ResourceTemplateManager to prevent shutdown issues
    if (resourceManager && resourceManager->isInitialized()) {
      resourceManager->clean();
    }
  }

protected:
  ResourceTemplateManager *resourceManager;
  std::shared_ptr<MockPlayer> mockPlayer;
  std::unique_ptr<InventoryComponent> testInventory;
  HammerEngine::ResourceHandle healthPotionHandle;
  HammerEngine::ResourceHandle swordHandle;
  HammerEngine::ResourceHandle oreHandle;
  HammerEngine::ResourceHandle coinsHandle;
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

BOOST_AUTO_TEST_CASE(TestAddStackableResource) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);

  // Test adding a stackable resource (Super Health Potion - max stack 20)
  bool added = inventory->addResource(healthPotionHandle, 5);
  BOOST_CHECK(added);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(healthPotionHandle), 5);
  BOOST_CHECK_EQUAL(inventory->getUsedSlots(), 1);
  BOOST_CHECK_EQUAL(inventory->getAvailableSlots(), 19);

  // Test adding more of the same resource (should stack)
  added = inventory->addResource(healthPotionHandle, 3);
  BOOST_CHECK(added);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(healthPotionHandle), 8);
  BOOST_CHECK_EQUAL(inventory->getUsedSlots(),
                    1); // Should still be 1 slot (stacked)
}

BOOST_AUTO_TEST_CASE(TestAddNonStackableResource) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);

  // Test adding equipment (Magic Sword - max stack 1, non-stackable)
  bool added = inventory->addResource(swordHandle, 1);
  BOOST_CHECK(added);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(swordHandle), 1);

  // Try adding another sword - should use another slot
  added = inventory->addResource(swordHandle, 1);
  BOOST_CHECK(added);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(swordHandle), 2);
  BOOST_CHECK_EQUAL(inventory->getUsedSlots(),
                    2); // Two separate slots for non-stackable items
}

BOOST_AUTO_TEST_CASE(TestRemoveResource) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);

  // Add some resources first
  inventory->addResource(healthPotionHandle, 10);
  inventory->addResource(swordHandle, 2);

  // Test removing partial quantity
  bool removed = inventory->removeResource(healthPotionHandle, 3);
  BOOST_CHECK(removed);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(healthPotionHandle), 7);

  // Test removing all of a resource
  removed = inventory->removeResource(swordHandle, 2);
  BOOST_CHECK(removed);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(swordHandle), 0);
  BOOST_CHECK(!inventory->hasResource(swordHandle));

  // Test removing more than available
  removed = inventory->removeResource(healthPotionHandle, 20);
  BOOST_CHECK(!removed);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(healthPotionHandle), 7);
}

BOOST_AUTO_TEST_CASE(TestHasResource) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);

  // Add some resources
  inventory->addResource(healthPotionHandle, 5);

  // Test hasResource with different quantities
  BOOST_CHECK(inventory->hasResource(healthPotionHandle));
  BOOST_CHECK(inventory->hasResource(healthPotionHandle, 1));
  BOOST_CHECK(inventory->hasResource(healthPotionHandle, 5));
  BOOST_CHECK(!inventory->hasResource(healthPotionHandle, 6));

  // Test non-existent resource
  HammerEngine::ResourceHandle invalidHandle(99999, 1);
  BOOST_CHECK(!inventory->hasResource(invalidHandle));
}

BOOST_AUTO_TEST_CASE(TestCapacityLimits) {
  auto smallInventory =
      std::make_unique<InventoryComponent>(mockPlayer.get(), 2);

  // Fill up the small inventory
  bool added1 = smallInventory->addResource(swordHandle, 1);
  bool added2 = smallInventory->addResource(coinsHandle, 1);
  BOOST_CHECK(added1);
  BOOST_CHECK(added2);
  BOOST_CHECK(smallInventory->isFull());
  BOOST_CHECK_EQUAL(smallInventory->getAvailableSlots(), 0);

  // Try to add when full
  bool added3 = smallInventory->addResource(healthPotionHandle, 1);
  BOOST_CHECK(!added3);
}

BOOST_AUTO_TEST_CASE(TestGetAllResources) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);
  inventory->addResource(healthPotionHandle, 5);
  inventory->addResource(swordHandle, 2);
  inventory->addResource(coinsHandle, 100);

  auto allResources = inventory->getAllResources();
  BOOST_CHECK_EQUAL(allResources.size(), 3);

  // Check quantities using handles
  auto foundHealthPotion = allResources.find(healthPotionHandle);
  auto foundSword = allResources.find(swordHandle);
  auto foundCoins = allResources.find(coinsHandle);

  BOOST_CHECK(foundHealthPotion != allResources.end());
  BOOST_CHECK(foundSword != allResources.end());
  BOOST_CHECK(foundCoins != allResources.end());

  if (foundHealthPotion != allResources.end()) {
    BOOST_CHECK_EQUAL(foundHealthPotion->second, 5);
  }
  if (foundSword != allResources.end()) {
    BOOST_CHECK_EQUAL(foundSword->second, 2);
  }
  if (foundCoins != allResources.end()) {
    BOOST_CHECK_EQUAL(foundCoins->second, 100);
  }

  // Test getting resource handles
  auto resourceHandles = inventory->getResourceHandles();
  BOOST_CHECK_GE(resourceHandles.size(),
                 3); // At least 3 different types of resources
  BOOST_CHECK(std::find(resourceHandles.begin(), resourceHandles.end(),
                        healthPotionHandle) != resourceHandles.end());
  BOOST_CHECK(std::find(resourceHandles.begin(), resourceHandles.end(),
                        swordHandle) != resourceHandles.end());
  BOOST_CHECK(std::find(resourceHandles.begin(), resourceHandles.end(),
                        coinsHandle) != resourceHandles.end());
}

BOOST_AUTO_TEST_CASE(TestClearInventory) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);
  inventory->addResource(healthPotionHandle, 5);
  inventory->addResource(swordHandle, 2);
  inventory->addResource(coinsHandle, 100);

  BOOST_CHECK_GT(inventory->getUsedSlots(), 0);

  inventory->clearInventory();

  BOOST_CHECK(inventory->isEmpty());
  BOOST_CHECK_EQUAL(inventory->getUsedSlots(), 0);
  BOOST_CHECK_EQUAL(inventory->getAvailableSlots(), inventory->getMaxSlots());
}

BOOST_AUTO_TEST_CASE(TestGetResourcesByCategory) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);
  inventory->addResource(healthPotionHandle, 5); // Item
  inventory->addResource(swordHandle, 1);        // Item
  inventory->addResource(oreHandle, 10);         // Material
  inventory->addResource(coinsHandle, 100);      // Currency

  // Test getting resources by category
  auto items = inventory->getResourcesByCategory(ResourceCategory::Item);
  auto materials =
      inventory->getResourcesByCategory(ResourceCategory::Material);
  auto currencies =
      inventory->getResourcesByCategory(ResourceCategory::Currency);

  // Items should contain health potion and sword
  BOOST_CHECK_GE(items.size(), 2);

  // Materials should contain ore
  BOOST_CHECK_GE(materials.size(), 1);

  // Currencies should contain coins
  BOOST_CHECK_GE(currencies.size(), 1);
}

BOOST_AUTO_TEST_CASE(TestSlotOperations) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 5);

  // Add some resources
  inventory->addResource(healthPotionHandle, 3);
  inventory->addResource(swordHandle, 1);

  // Test getting slot information
  BOOST_CHECK_GE(inventory->getUsedSlots(), 2);

  // Test slot access (assuming slots are filled sequentially)
  if (inventory->getUsedSlots() > 0) {
    const auto &slot0 = inventory->getSlot(0);
    BOOST_CHECK(!slot0.isEmpty());
    BOOST_CHECK(slot0.resourceHandle.isValid());
    BOOST_CHECK_GT(slot0.quantity, 0);
  }
}

BOOST_AUTO_TEST_CASE(TestCanAddResource) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 2);

  // Should be able to add to empty inventory
  BOOST_CHECK(inventory->canAddResource(healthPotionHandle, 5));

  // Add a resource
  inventory->addResource(healthPotionHandle, 5);

  // Should still be able to add more of the same (stacking)
  BOOST_CHECK(inventory->canAddResource(healthPotionHandle, 5));

  // Fill up remaining slots
  inventory->addResource(swordHandle, 1);

  // Should not be able to add a new type of resource when full
  BOOST_CHECK(!inventory->canAddResource(oreHandle, 1));
}

BOOST_AUTO_TEST_CASE(TestResourceHandleOperations) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);

  // Test handle-based resource operations
  bool added = inventory->addResource(healthPotionHandle, 5);
  BOOST_CHECK(added);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(healthPotionHandle), 5);

  // Test hasResource with handle
  BOOST_CHECK(inventory->hasResource(healthPotionHandle));
  BOOST_CHECK(inventory->hasResource(healthPotionHandle, 5));
  BOOST_CHECK(!inventory->hasResource(healthPotionHandle, 6));

  // Test removing with handle
  bool removed = inventory->removeResource(healthPotionHandle, 2);
  BOOST_CHECK(removed);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(healthPotionHandle), 3);

  // Test non-existent resource
  HammerEngine::ResourceHandle invalidHandle(99999, 1);
  BOOST_CHECK(!inventory->hasResource(invalidHandle));
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(invalidHandle), 0);
}

BOOST_AUTO_TEST_CASE(TestUtilityMethods) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);

  // Add some resources
  inventory->addResource(healthPotionHandle, 5);
  inventory->addResource(coinsHandle, 100);

  // Test methods that are implemented
  BOOST_CHECK_NO_THROW(inventory->compactInventory());

  // Test can add resource method with a valid resource
  BOOST_CHECK(inventory->canAddResource(swordHandle, 1));
}

BOOST_AUTO_TEST_CASE(TestInvalidOperations) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 10);

  // Test adding zero or negative quantities
  bool added = inventory->addResource(healthPotionHandle, 0);
  BOOST_CHECK(!added);

  added = inventory->addResource(healthPotionHandle, -5);
  BOOST_CHECK(!added);

  // Test removing from empty inventory
  HammerEngine::ResourceHandle nonExistentHandle(99999, 1);
  bool removed = inventory->removeResource(nonExistentHandle, 1);
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
  sourceInventory->addResource(healthPotionHandle, 10);
  sourceInventory->addResource(coinsHandle, 100);

  // Test transfer using handle
  bool transferred =
      sourceInventory->transferTo(*targetInventory, healthPotionHandle, 5);
  BOOST_CHECK(transferred);
  BOOST_CHECK_EQUAL(sourceInventory->getResourceQuantity(healthPotionHandle),
                    5);
  BOOST_CHECK_EQUAL(targetInventory->getResourceQuantity(healthPotionHandle),
                    5);

  // Test transfer using handle for coins
  bool coinsTransferred =
      sourceInventory->transferTo(*targetInventory, coinsHandle, 50);
  BOOST_CHECK(coinsTransferred);
  BOOST_CHECK_EQUAL(sourceInventory->getResourceQuantity(coinsHandle), 50);
  BOOST_CHECK_EQUAL(targetInventory->getResourceQuantity(coinsHandle), 50);

  // Test transfer more than available
  transferred = sourceInventory->transferTo(*targetInventory, coinsHandle, 200);
  BOOST_CHECK(!transferred);
  BOOST_CHECK_EQUAL(sourceInventory->getResourceQuantity(coinsHandle), 50);
  BOOST_CHECK_EQUAL(targetInventory->getResourceQuantity(coinsHandle), 50);
}

BOOST_AUTO_TEST_CASE(TestStackLimits) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);

  // Super Health Potion has maxStackSize of 20
  bool added = inventory->addResource(healthPotionHandle, 20);
  BOOST_CHECK(added);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(healthPotionHandle), 20);
  BOOST_CHECK_EQUAL(inventory->getUsedSlots(), 1);

  // Adding one more should create a new stack
  added = inventory->addResource(healthPotionHandle, 1);
  BOOST_CHECK(added);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(healthPotionHandle), 21);
  BOOST_CHECK_EQUAL(inventory->getUsedSlots(), 2);
}

BOOST_AUTO_TEST_CASE(TestCompactInventory) {
  auto inventory = std::make_unique<InventoryComponent>(mockPlayer.get(), 20);

  // Add resources in a fragmented way
  inventory->addResource(healthPotionHandle, 5);
  inventory->addResource(swordHandle, 1);
  inventory->addResource(healthPotionHandle, 3);

  // Check initial state
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(healthPotionHandle), 8);

  // Compact inventory
  inventory->compactInventory();

  // After compacting, should still have same quantities but possibly different
  // slot arrangement
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(healthPotionHandle), 8);
  BOOST_CHECK_EQUAL(inventory->getResourceQuantity(swordHandle), 1);
}

BOOST_AUTO_TEST_SUITE_END()