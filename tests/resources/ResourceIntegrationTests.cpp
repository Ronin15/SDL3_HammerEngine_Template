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
#include "managers/ResourceManager.hpp"
#include <boost/test/unit_test.hpp>

// Force ResourceManager reset for test isolation
struct ResourceManagerResetter {
  ResourceManagerResetter() {
    RESOURCE_INFO("ResourceManagerResetter: before clean");
    ResourceManager::Instance().clean();
    RESOURCE_INFO("ResourceManagerResetter: after clean, before init");
    ResourceManager::Instance().init();
    RESOURCE_INFO("ResourceManagerResetter: after init");
  }
};
static ResourceManagerResetter resourceManagerResetterInstance;

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../mocks/MockPlayer.hpp"
#include "core/Logger.hpp"
#include "entities/Resource.hpp"
#include "entities/resources/InventoryComponent.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "managers/ResourceManager.hpp"

class ResourceIntegrationTestFixture {
public:
  ResourceIntegrationTestFixture() {
    // Initialize ResourceManager
    resourceManager = &ResourceManager::Instance();

    // Ensure ResourceManager is initialized with default resources
    if (!resourceManager->isInitialized()) {
      resourceManager->init();
    }

    // Create test inventory components to simulate entities
    playerInventory = std::make_unique<InventoryComponent>(
        nullptr, 50); // Player with 50 slots
    npcInventory = std::make_unique<InventoryComponent>(
        nullptr, 60); // NPC with 60 slots (enough for 5000 iron ore)

    // Get test resources
    RESOURCE_DEBUG("Before getResourceTemplate health_potion");
    healthPotion = resourceManager->getResourceTemplate("health_potion");
    RESOURCE_DEBUG("After getResourceTemplate health_potion");
    RESOURCE_DEBUG("Before getResourceTemplate iron_sword");
    ironSword = resourceManager->getResourceTemplate("iron_sword");
    RESOURCE_DEBUG("After getResourceTemplate iron_sword");
    RESOURCE_DEBUG("Before getResourceTemplate iron_ore");
    ironOre = resourceManager->getResourceTemplate("iron_ore");
    RESOURCE_DEBUG("After getResourceTemplate iron_ore");
    RESOURCE_DEBUG("Before getResourceTemplate gold");
    gold = resourceManager->getResourceTemplate("gold");
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
  }

protected:
  ResourceManager *resourceManager;
  std::unique_ptr<InventoryComponent> playerInventory;
  std::unique_ptr<InventoryComponent> npcInventory;
  std::shared_ptr<Resource> healthPotion;
  std::shared_ptr<Resource> ironSword;
  std::shared_ptr<Resource> ironOre;
  std::shared_ptr<Resource> gold;
};

BOOST_FIXTURE_TEST_SUITE(ResourceIntegrationTestSuite,
                         ResourceIntegrationTestFixture)

BOOST_AUTO_TEST_CASE(TestPlayerInventoryIntegration) {
  // Test that inventory component works properly
  BOOST_REQUIRE(playerInventory != nullptr);

  // Test inventory capacity (should be 50 as defined)
  BOOST_CHECK_EQUAL(playerInventory->getMaxSlots(), 50);
  BOOST_CHECK(playerInventory->isEmpty());

  // Test adding resources to inventory
  bool added = playerInventory->addResource("health_potion", 10);
  BOOST_CHECK(added);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("health_potion"), 10);
  BOOST_CHECK(!playerInventory->isEmpty());

  // Test removing resources from inventory
  bool removed = playerInventory->removeResource("health_potion", 3);
  BOOST_CHECK(removed);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("health_potion"), 7);

  // Test inventory has resource check
  BOOST_CHECK(playerInventory->hasResource("health_potion"));
  BOOST_CHECK(playerInventory->hasResource("health_potion", 5));
  BOOST_CHECK(!playerInventory->hasResource("health_potion", 10));
  BOOST_CHECK(!playerInventory->hasResource("iron_sword"));
}

BOOST_AUTO_TEST_CASE(TestNPCInventoryIntegration) {
  // Test that NPC inventory component works properly
  BOOST_REQUIRE(npcInventory != nullptr);

  // Test NPC inventory capacity (should be 60 as defined)
  BOOST_CHECK_EQUAL(npcInventory->getMaxSlots(), 60);
  BOOST_CHECK(npcInventory->isEmpty());

  // Test adding resources to NPC inventory
  bool added = npcInventory->addResource("iron_ore", 15);
  BOOST_CHECK(added);
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity("iron_ore"), 15);

  // Test removing resources from NPC inventory
  bool removed = npcInventory->removeResource("iron_ore", 5);
  BOOST_CHECK(removed);
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity("iron_ore"), 10);

  // Test NPC inventory has resource check
  BOOST_CHECK(npcInventory->hasResource("iron_ore"));
  BOOST_CHECK(npcInventory->hasResource("iron_ore", 8));
  BOOST_CHECK(!npcInventory->hasResource("iron_ore", 15));
  BOOST_CHECK(!npcInventory->hasResource("health_potion"));
}

BOOST_AUTO_TEST_CASE(TestResourceTransferBetweenEntities) {
  // Setup: Give player inventory some resources
  playerInventory->addResource("health_potion", 20);
  playerInventory->addResource("gold", 100);

  // Setup: Give NPC inventory some resources
  npcInventory->addResource("iron_sword", 1);
  npcInventory->addResource("iron_ore", 50);

  // Test transferring resources from player to NPC inventory
  BOOST_REQUIRE(playerInventory->hasResource("health_potion", 5));
  BOOST_REQUIRE(playerInventory->removeResource("health_potion", 5));
  BOOST_REQUIRE(npcInventory->addResource("health_potion", 5));

  // Verify transfer
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("health_potion"), 15);
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity("health_potion"), 5);

  // Test transferring resources from NPC to player inventory
  BOOST_REQUIRE(npcInventory->hasResource("iron_ore", 10));
  BOOST_REQUIRE(npcInventory->removeResource("iron_ore", 10));
  BOOST_REQUIRE(playerInventory->addResource("iron_ore", 10));

  // Verify transfer
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity("iron_ore"), 40);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("iron_ore"), 10);
}

BOOST_AUTO_TEST_CASE(TestTradingScenario) {
  // Setup a trading scenario: Player trades gold for NPC's equipment

  // Initial setup
  playerInventory->addResource("gold", 500);  // Player has gold
  npcInventory->addResource("iron_sword", 3); // NPC has swords

  const int swordPrice = 100;
  const int swordsToTrade = 2;
  const int totalCost = swordPrice * swordsToTrade;

  // Verify preconditions
  BOOST_REQUIRE(playerInventory->hasResource("gold", totalCost));
  BOOST_REQUIRE(npcInventory->hasResource("iron_sword", swordsToTrade));

  // Execute trade: Player gives gold, receives swords
  bool playerPaysGold = playerInventory->removeResource("gold", totalCost);
  bool npcGivesSwords =
      npcInventory->removeResource("iron_sword", swordsToTrade);

  BOOST_REQUIRE(playerPaysGold);
  BOOST_REQUIRE(npcGivesSwords);

  // Complete the trade
  bool npcReceivesGold = npcInventory->addResource("gold", totalCost);
  bool playerReceivesSwords =
      playerInventory->addResource("iron_sword", swordsToTrade);

  BOOST_REQUIRE(npcReceivesGold);
  BOOST_REQUIRE(playerReceivesSwords);

  // Verify final state
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("gold"),
                    500 - totalCost);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("iron_sword"),
                    swordsToTrade);
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity("gold"), totalCost);
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity("iron_sword"),
                    3 - swordsToTrade);
}

BOOST_AUTO_TEST_CASE(TestResourceManagement) {
  // Test basic resource management operations
  playerInventory->addResource("iron_sword", 2);

  // Test basic resource management
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("iron_sword"), 2);
  BOOST_CHECK(playerInventory->hasResource("iron_sword"));

  // Test removing equipment
  bool removed = playerInventory->removeResource("iron_sword", 1);
  BOOST_CHECK(removed);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("iron_sword"), 1);

  // Test consuming resource
  playerInventory->addResource("health_potion", 1);
  bool consumed = playerInventory->removeResource("health_potion", 1);
  BOOST_CHECK(consumed);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("health_potion"), 0);
}

BOOST_AUTO_TEST_CASE(TestResourceByCategory) {
  // Setup: Add various resources to player inventory
  playerInventory->addResource("health_potion", 5); // Item/Consumable
  playerInventory->addResource("iron_sword", 1);    // Item/Equipment
  playerInventory->addResource("iron_ore", 20);     // Material
  playerInventory->addResource("gold", 100);        // Currency

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
    if (playerInventory->addResource("iron_sword", 1)) {
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
    if (npcInventory->addResource("iron_sword", 1)) {
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
  playerInventory->addResource("health_potion", 10);
  playerInventory->addResource("iron_sword", 2);
  playerInventory->addResource("gold", 500);

  // Note: InventoryComponent doesn't have serialize/deserialize methods
  // This test focuses on verifying resource state can be queried

  // Verify inventory state can be queried
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("health_potion"), 10);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("iron_sword"), 2);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("gold"), 500);

  // Test NPC inventory resources as well
  npcInventory->addResource("iron_ore", 25);
  npcInventory->addResource("gold", 200);

  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity("iron_ore"), 25);
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity("gold"), 200);
}

BOOST_AUTO_TEST_CASE(TestResourceConsumption) {
  // Test consuming resources (like using health potions)
  playerInventory->addResource("health_potion", 5);

  // Simulate using a health potion
  BOOST_REQUIRE(playerInventory->hasResource("health_potion", 1));
  bool consumed = playerInventory->removeResource("health_potion", 1);
  BOOST_CHECK(consumed);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("health_potion"), 4);

  // Try to consume more than available
  bool overConsume = playerInventory->removeResource("health_potion", 10);
  BOOST_CHECK(!overConsume);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("health_potion"),
                    4); // Should remain unchanged
}

BOOST_AUTO_TEST_CASE(TestComplexTradingChain) {
  // Test a complex trading chain: Player -> NPC -> Trader
  auto traderInventory = std::make_unique<InventoryComponent>(nullptr, 30);

  // Initial setup
  playerInventory->addResource("gold", 1000);
  npcInventory->addResource("iron_ore", 100);
  traderInventory->addResource("iron_sword", 10);

  // Step 1: Player trades gold for iron ore from NPC
  const int orePrice = 5;
  const int oreQuantity = 20;
  const int oreCost = orePrice * oreQuantity;

  BOOST_REQUIRE(playerInventory->removeResource("gold", oreCost));
  BOOST_REQUIRE(npcInventory->removeResource("iron_ore", oreQuantity));
  BOOST_REQUIRE(npcInventory->addResource("gold", oreCost));
  BOOST_REQUIRE(playerInventory->addResource("iron_ore", oreQuantity));

  // Step 2: Player trades iron ore for sword from Trader
  const int swordOrePrice = 10; // 10 ore per sword
  const int swordsWanted = 2;
  const int oreNeeded = swordOrePrice * swordsWanted;

  BOOST_REQUIRE(playerInventory->removeResource("iron_ore", oreNeeded));
  BOOST_REQUIRE(traderInventory->removeResource("iron_sword", swordsWanted));
  BOOST_REQUIRE(traderInventory->addResource("iron_ore", oreNeeded));
  BOOST_REQUIRE(playerInventory->addResource("iron_sword", swordsWanted));

  // Verify final state
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("gold"),
                    1000 - oreCost);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("iron_ore"),
                    oreQuantity - oreNeeded);
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("iron_sword"),
                    swordsWanted);

  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity("gold"), oreCost);
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity("iron_ore"),
                    100 - oreQuantity);

  BOOST_CHECK_EQUAL(traderInventory->getResourceQuantity("iron_ore"),
                    oreNeeded);
  BOOST_CHECK_EQUAL(traderInventory->getResourceQuantity("iron_sword"),
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
  playerInventory->addResource("gold", 10000);
  npcInventory->addResource("iron_ore", 5000);

  std::vector<std::thread> threads;
  std::atomic<int> successfulPlayerOps{0};
  std::atomic<int> successfulNPCOps{0};

  for (int i = 0; i < NUM_THREADS; ++i) {
    threads.emplace_back([&, i]() {
      for (int j = 0; j < OPERATIONS_PER_THREAD; ++j) {
        // Test concurrent player operations
        if (playerInventory->addResource("health_potion", 1)) {
          if (playerInventory->removeResource("health_potion", 1)) {
            successfulPlayerOps++;
          }
        }

        // Test concurrent NPC operations
        if (npcInventory->addResource("iron_sword", 1)) {
          if (npcInventory->removeResource("iron_sword", 1)) {
            successfulNPCOps++;
          }
        }

        std::this_thread::sleep_for(std::chrono::microseconds(1));
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  // Verify operations were successful (should be mostly successful due to
  // thread safety)
  BOOST_CHECK(successfulPlayerOps.load() > 0);
  BOOST_CHECK(successfulNPCOps.load() > 0);

  // Verify original resources are still intact
  BOOST_CHECK_EQUAL(playerInventory->getResourceQuantity("gold"), 10000);
  BOOST_CHECK_EQUAL(npcInventory->getResourceQuantity("iron_ore"), 5000);
}

BOOST_AUTO_TEST_SUITE_END()