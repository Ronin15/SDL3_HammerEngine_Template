/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include <cstdio>
__attribute__((constructor)) static void print_startup() {
  printf("[PRINT] ResourceIntegrationTests binary startup!\n");
  fflush(stdout);
}
#define BOOST_TEST_MODULE ResourceIntegrationTests
#include "core/Logger.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include <boost/test/unit_test.hpp>

// Force ResourceTemplateManager reset for test isolation
struct ResourceTemplateManagerResetter {
  ResourceTemplateManagerResetter() {
    RESOURCE_INFO("ResourceTemplateManagerResetter: before clean");
    // Only clean if already initialized to avoid double cleanup
    if (ResourceTemplateManager::Instance().isInitialized()) {
      ResourceTemplateManager::Instance().clean();
    }
    RESOURCE_INFO("ResourceTemplateManagerResetter: after clean, before init");
    ResourceTemplateManager::Instance().init();
    RESOURCE_INFO("ResourceTemplateManagerResetter: after init");
  }

  ~ResourceTemplateManagerResetter() {
    RESOURCE_INFO("ResourceTemplateManagerResetter: destructor - before clean");
    // Only clean if still initialized to avoid double cleanup
    if (ResourceTemplateManager::Instance().isInitialized()) {
      ResourceTemplateManager::Instance().clean();
    }
    RESOURCE_INFO("ResourceTemplateManagerResetter: destructor - after clean");
  }
};
static ResourceTemplateManagerResetter resourceTemplateManagerResetterInstance;

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "core/ThreadSystem.hpp"
#include "entities/Resource.hpp"
#include "entities/resources/InventoryComponent.hpp"
#include "managers/ResourceTemplateManager.hpp"

class ResourceIntegrationTestFixture {
public:
  ResourceIntegrationTestFixture() {
    // Initialize ThreadSystem first for threading tests
    threadSystem = &HammerEngine::ThreadSystem::Instance();
    if (threadSystem->isShutdown() || threadSystem->getThreadCount() == 0) {
      bool initSuccess = threadSystem->init();
      if (!initSuccess && threadSystem->getThreadCount() == 0) {
        throw std::runtime_error(
            "Failed to initialize ThreadSystem for threading tests");
      }
    }

    // Initialize ResourceTemplateManager
    resourceManager = &ResourceTemplateManager::Instance();

    // Ensure ResourceTemplateManager is initialized with default resources
    if (!resourceManager->isInitialized()) {
      resourceManager->init();
    }

    // Create test inventory components to simulate entities
    playerInventory = std::make_unique<InventoryComponent>(
        nullptr, 50); // Player with 50 slots
    npcInventory = std::make_unique<InventoryComponent>(
        nullptr, 60); // NPC with 60 slots (enough for 5000 iron ore)

    // Get test resource handles by name (more reliable than hardcoding)
    healthPotionHandle =
        resourceManager->getHandleByName("Super Health Potion");
    ironSwordHandle = resourceManager->getHandleByName("Magic Sword");
    ironOreHandle = resourceManager->getHandleByName("Mithril Ore");
    goldHandle = resourceManager->getHandleByName("Platinum Coins");

    // Get test resources using handles
    RESOURCE_DEBUG("Before getResourceTemplate super_health_potion");
    BOOST_REQUIRE(healthPotionHandle.isValid());
    healthPotion = resourceManager->getResourceTemplate(healthPotionHandle);
    RESOURCE_DEBUG("After getResourceTemplate super_health_potion");

    RESOURCE_DEBUG("Before getResourceTemplate magic_sword");
    BOOST_REQUIRE(ironSwordHandle.isValid());
    ironSword = resourceManager->getResourceTemplate(ironSwordHandle);
    RESOURCE_DEBUG("After getResourceTemplate magic_sword");

    RESOURCE_DEBUG("Before getResourceTemplate mithril_ore");
    BOOST_REQUIRE(ironOreHandle.isValid());
    ironOre = resourceManager->getResourceTemplate(ironOreHandle);
    RESOURCE_DEBUG("After getResourceTemplate mithril_ore");

    RESOURCE_DEBUG("Before getResourceTemplate platinum_coins");
    BOOST_REQUIRE(goldHandle.isValid());
    gold = resourceManager->getResourceTemplate(goldHandle);
    RESOURCE_DEBUG("After getResourceTemplate gold");

    BOOST_REQUIRE(healthPotion != nullptr);
    BOOST_REQUIRE(ironSword != nullptr);
    BOOST_REQUIRE(ironOre != nullptr);
    BOOST_REQUIRE(gold != nullptr);
    BOOST_REQUIRE(playerInventory != nullptr);
    BOOST_REQUIRE(npcInventory != nullptr);
  }

  ~ResourceIntegrationTestFixture() {
    playerInventory.reset();
    npcInventory.reset();
    // Note: Don't clean ThreadSystem here as it's shared across tests
  }

protected:
  ResourceTemplateManager *resourceManager;
  HammerEngine::ThreadSystem *threadSystem;
  std::unique_ptr<InventoryComponent> playerInventory;
  std::unique_ptr<InventoryComponent> npcInventory;
  std::shared_ptr<Resource> healthPotion;
  std::shared_ptr<Resource> ironSword;
  std::shared_ptr<Resource> ironOre;
  std::shared_ptr<Resource> gold;

  // Resource handles for easy access
  HammerEngine::ResourceHandle healthPotionHandle;
  HammerEngine::ResourceHandle ironSwordHandle;
  HammerEngine::ResourceHandle ironOreHandle;
  HammerEngine::ResourceHandle goldHandle;
};
BOOST_FIXTURE_TEST_SUITE(ResourceIntegrationTestSuite,
                         ResourceIntegrationTestFixture)

BOOST_AUTO_TEST_CASE(TestPlayerInventoryIntegration) {
  // Test that player inventory component works properly
  BOOST_REQUIRE(playerInventory != nullptr);

  // Test player inventory capacity (should be 50 as defined)
  BOOST_CHECK_EQUAL(playerInventory->getMaxSlots(), 50);
  BOOST_CHECK(playerInventory->isEmpty());

  // Test adding resources to player inventory using handles
  bool added = playerInventory->addResource(healthPotionHandle, 10);
  BOOST_CHECK(added);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(healthPotionHandle),
                    10);

  // Test removing resources from inventory
  bool removed = playerInventory->removeResource(healthPotionHandle, 3);
  BOOST_CHECK(removed);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(healthPotionHandle),
                    7);

  // Test inventory has resource check
  BOOST_CHECK(playerInventory->hasResource(healthPotionHandle));
  BOOST_CHECK(playerInventory->hasResource(healthPotionHandle, 5));
  BOOST_CHECK(!playerInventory->hasResource(healthPotionHandle, 10));
  BOOST_CHECK(!playerInventory->hasResource(ironSwordHandle));
}

BOOST_AUTO_TEST_CASE(TestNPCInventoryIntegration) {
  // Test that NPC inventory component works properly
  BOOST_REQUIRE(npcInventory != nullptr);

  // Test NPC inventory capacity (should be 60 as defined)
  BOOST_CHECK_EQUAL(npcInventory->getMaxSlots(), 60);
  BOOST_CHECK(npcInventory->isEmpty());

  // Test adding resources to NPC inventory using handles
  bool added = npcInventory->addResource(ironOreHandle, 15);
  BOOST_CHECK(added);
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity(ironOreHandle), 15);

  // Test removing resources from NPC inventory
  bool removed = npcInventory->removeResource(ironOreHandle, 5);
  BOOST_CHECK(removed);
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity(ironOreHandle), 10);

  // Test NPC inventory has resource check
  BOOST_CHECK(npcInventory->hasResource(ironOreHandle));
  BOOST_CHECK(npcInventory->hasResource(ironOreHandle, 8));
  BOOST_CHECK(!npcInventory->hasResource(ironOreHandle, 15));
  BOOST_CHECK(!npcInventory->hasResource(healthPotionHandle));
}

BOOST_AUTO_TEST_CASE(TestResourceTransferBetweenEntities) {
  // Setup: Give player inventory some resources
  playerInventory->addResource(healthPotionHandle, 20);
  playerInventory->addResource(goldHandle, 100);

  // Setup: Give NPC inventory some resources
  npcInventory->addResource(ironSwordHandle, 1);
  npcInventory->addResource(ironOreHandle, 50);

  // Test transferring resources from player to NPC inventory
  BOOST_REQUIRE(playerInventory->hasResource(healthPotionHandle, 5));
  BOOST_REQUIRE(playerInventory->removeResource(healthPotionHandle, 5));
  BOOST_REQUIRE(npcInventory->addResource(healthPotionHandle, 5));

  // Check that quantities are correct after transfer
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(healthPotionHandle),
                    15);
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity(healthPotionHandle), 5);

  // Test transferring materials from NPC to player inventory
  BOOST_REQUIRE(npcInventory->hasResource(ironOreHandle, 10));
  BOOST_REQUIRE(npcInventory->removeResource(ironOreHandle, 10));
  BOOST_REQUIRE(playerInventory->addResource(ironOreHandle, 10));

  // Check quantities after transfer
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity(ironOreHandle), 40);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(ironOreHandle), 10);
}

BOOST_AUTO_TEST_CASE(TestTradingScenario) {
  // Setup a trading scenario: Player trades gold for NPC's equipment

  // Initial setup
  playerInventory->addResource(goldHandle, 500); // Player has gold
  npcInventory->addResource(ironSwordHandle, 3); // NPC has swords

  const int swordPrice = 100;
  const int swordsToTrade = 2;
  const int totalCost = swordPrice * swordsToTrade;

  // Verify preconditions
  BOOST_REQUIRE(playerInventory->hasResource(goldHandle, totalCost));
  BOOST_REQUIRE(npcInventory->hasResource(ironSwordHandle, swordsToTrade));

  // Execute trade: Player gives gold, receives swords
  bool playerPaysGold = playerInventory->removeResource(goldHandle, totalCost);
  bool npcGivesSwords =
      npcInventory->removeResource(ironSwordHandle, swordsToTrade);

  BOOST_REQUIRE(playerPaysGold);
  BOOST_REQUIRE(npcGivesSwords);

  // Complete the trade
  bool npcReceivesGold = npcInventory->addResource(goldHandle, totalCost);
  bool playerReceivesSwords =
      playerInventory->addResource(ironSwordHandle, swordsToTrade);

  BOOST_REQUIRE(npcReceivesGold);
  BOOST_REQUIRE(playerReceivesSwords);

  // Verify final state
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(goldHandle),
                    500 - totalCost);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(ironSwordHandle),
                    swordsToTrade);
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity(goldHandle), totalCost);
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity(ironSwordHandle),
                    3 - swordsToTrade);
}

BOOST_AUTO_TEST_CASE(TestResourceManagement) {
  // Test basic resource management operations
  playerInventory->addResource(ironSwordHandle, 2);

  // Test basic resource management
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(ironSwordHandle), 2);
  BOOST_CHECK(playerInventory->hasResource(ironSwordHandle));

  // Test removing equipment
  bool removed = playerInventory->removeResource(ironSwordHandle, 1);
  BOOST_CHECK(removed);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(ironSwordHandle), 1);

  // Test consuming resource
  playerInventory->addResource(healthPotionHandle, 1);
  bool consumed = playerInventory->removeResource(healthPotionHandle, 1);
  BOOST_CHECK(consumed);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(healthPotionHandle),
                    0);
}

BOOST_AUTO_TEST_CASE(TestResourceByCategory) {
  // Setup: Add various resources to player inventory
  playerInventory->addResource(healthPotionHandle, 5); // Item/Consumable
  playerInventory->addResource(ironSwordHandle, 1);    // Item/Equipment
  playerInventory->addResource(ironOreHandle, 20);     // Material
  playerInventory->addResource(goldHandle, 100);       // Currency

  // Test getting resources by category
  auto itemResources =
      playerInventory->getResourcesByCategory(ResourceCategory::Item);
  BOOST_CHECK_EQUAL(itemResources.size(), 2); // health_potion and iron_sword

  auto materialResources =
      playerInventory->getResourcesByCategory(ResourceCategory::Material);
  BOOST_CHECK_EQUAL(materialResources.size(), 1); // iron_ore

  auto currencyResources =
      playerInventory->getResourcesByCategory(ResourceCategory::Currency);
  BOOST_CHECK_EQUAL(currencyResources.size(), 1); // gold

  // Note: InventoryComponent doesn't have getResourcesByType, only by category
  // So we remove the ResourceType tests
}

BOOST_AUTO_TEST_CASE(TestInventoryCapacityLimits) {
  // Test Player inventory capacity (50 slots)

  // Fill player inventory with iron swords
  int swordsAdded = 0;
  for (int i = 0; i < 55; ++i) { // Try to add more than capacity
    if (playerInventory->addResource(ironSwordHandle, 1)) {
      swordsAdded++;
    } else {
      break; // Inventory full
    }
  }

  // Should only be able to add 50 (capacity limit)
  BOOST_CHECK_EQUAL(swordsAdded, 50);
  BOOST_CHECK_EQUAL(playerInventory->getUsedSlots(), 50);
  BOOST_CHECK_EQUAL(playerInventory->getAvailableSlots(), 0);

  // Test NPC inventory capacity (60 slots)
  int npcItemsAdded = 0;
  for (int i = 0; i < 65; ++i) { // Try to add more than capacity
    if (npcInventory->addResource(ironSwordHandle, 1)) {
      npcItemsAdded++;
    } else {
      break; // Inventory full
    }
  }

  // Should only be able to add 60 (capacity limit)
  BOOST_CHECK_EQUAL(npcItemsAdded, 60);
  BOOST_CHECK_EQUAL(npcInventory->getUsedSlots(), 60);
  BOOST_CHECK_EQUAL(npcInventory->getAvailableSlots(), 0);
}

BOOST_AUTO_TEST_CASE(TestResourceSerialization) {
  // Setup: Add resources to player inventory
  playerInventory->addResource(healthPotionHandle, 10);
  playerInventory->addResource(ironSwordHandle, 2);
  playerInventory->addResource(goldHandle, 500);

  // Note: InventoryComponent doesn't have serialize/deserialize methods
  // This test focuses on verifying resource state can be queried

  // Verify inventory state can be queried
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(healthPotionHandle),
                    10);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(ironSwordHandle), 2);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(goldHandle), 500);

  // Test NPC inventory resources as well
  npcInventory->addResource(ironOreHandle, 25);
  npcInventory->addResource(goldHandle, 200);

  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity(ironOreHandle), 25);
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity(goldHandle), 200);
}

BOOST_AUTO_TEST_CASE(TestResourceConsumption) {
  // Test consuming resources (like using health potions)
  playerInventory->addResource(healthPotionHandle, 5);

  // Simulate using a health potion
  BOOST_REQUIRE(playerInventory->hasResource(healthPotionHandle, 1));
  bool consumed = playerInventory->removeResource(healthPotionHandle, 1);
  BOOST_CHECK(consumed);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(healthPotionHandle),
                    4);

  // Try to consume more than available
  bool overConsume = playerInventory->removeResource(healthPotionHandle, 10);
  BOOST_CHECK(!overConsume);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(healthPotionHandle),
                    4); // Should remain unchanged
}

BOOST_AUTO_TEST_CASE(TestComplexTradingChain) {
  // Test a complex trading chain: Player -> NPC -> Trader
  auto traderInventory = std::make_unique<InventoryComponent>(nullptr, 30);

  // Initial setup
  playerInventory->addResource(goldHandle, 1000);
  npcInventory->addResource(ironOreHandle, 100);
  traderInventory->addResource(ironSwordHandle, 10);

  // Step 1: Player trades gold for iron ore from NPC
  const int orePrice = 5;
  const int oreQuantity = 20;
  const int oreCost = orePrice * oreQuantity;

  BOOST_REQUIRE(playerInventory->removeResource(goldHandle, oreCost));
  BOOST_REQUIRE(npcInventory->removeResource(ironOreHandle, oreQuantity));
  BOOST_REQUIRE(npcInventory->addResource(goldHandle, oreCost));
  BOOST_REQUIRE(playerInventory->addResource(ironOreHandle, oreQuantity));

  // Step 2: Player trades iron ore for sword from Trader
  const int swordOrePrice = 10; // 10 ore per sword
  const int swordsWanted = 2;
  const int oreNeeded = swordOrePrice * swordsWanted;

  BOOST_REQUIRE(playerInventory->removeResource(ironOreHandle, oreNeeded));
  BOOST_REQUIRE(traderInventory->removeResource(ironSwordHandle, swordsWanted));
  BOOST_REQUIRE(traderInventory->addResource(ironOreHandle, oreNeeded));
  BOOST_REQUIRE(playerInventory->addResource(ironSwordHandle, swordsWanted));

  // Verify final state
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(goldHandle),
                    1000 - oreCost);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(ironOreHandle),
                    oreQuantity - oreNeeded);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(ironSwordHandle),
                    swordsWanted);

  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity(goldHandle), oreCost);
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity(ironOreHandle),
                    100 - oreQuantity);

  BOOST_CHECK_EQUAL(traderInventory->getResourceQuantity(ironOreHandle),
                    oreNeeded);
  BOOST_CHECK_EQUAL(traderInventory->getResourceQuantity(ironSwordHandle),
                    10 - swordsWanted);
}

BOOST_AUTO_TEST_CASE(TestConcurrentResourceOperations) {
  // Clear inventories from previous tests to ensure clean state
  playerInventory->clearInventory();
  npcInventory->clearInventory();

  // Test thread safety of resource operations
  const int NUM_THREADS = 5;
  const int OPERATIONS_PER_THREAD = 20;

  // Pre-populate with resources
  playerInventory->addResource(goldHandle, 10000);
  npcInventory->addResource(ironOreHandle, 5000);

  std::vector<std::future<void>> futures;
  std::atomic<int> successfulPlayerOps{0};
  std::atomic<int> successfulNPCOps{0};

  for (int i = 0; i < NUM_THREADS; ++i) {
    auto future = threadSystem->enqueueTaskWithResult(
        [=, this, &successfulPlayerOps, &successfulNPCOps]() -> void {
          for (int j = 0; j < OPERATIONS_PER_THREAD; ++j) {
            // Test concurrent player operations
            if (playerInventory->addResource(healthPotionHandle, 1)) {
              if (playerInventory->removeResource(healthPotionHandle, 1)) {
                successfulPlayerOps.fetch_add(1, std::memory_order_relaxed);
              }
            }

            // Test concurrent NPC operations
            if (npcInventory->addResource(ironSwordHandle, 1)) {
              if (npcInventory->removeResource(ironSwordHandle, 1)) {
                successfulNPCOps.fetch_add(1, std::memory_order_relaxed);
              }
            }

            std::this_thread::sleep_for(std::chrono::microseconds(1));
          }
        },
        HammerEngine::TaskPriority::Normal, "ResourceIntegrationTask");

    futures.push_back(std::move(future));
  }

  for (auto &future : futures) {
    future.wait();
  }

  // Verify operations were successful (should be mostly successful due to
  // thread safety)
  BOOST_CHECK(successfulPlayerOps.load() > 0);
  BOOST_CHECK(successfulNPCOps.load() > 0);

  // Verify original resources are still intact
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity(goldHandle), 10000);
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity(ironOreHandle), 5000);
}

BOOST_AUTO_TEST_SUITE_END()