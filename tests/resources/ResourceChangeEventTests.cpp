/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE ResourceChangeEventTests
#include <boost/test/unit_test.hpp>

#include <memory>
#include <sstream>
#include <string>

#include "core/Logger.hpp"
#include "entities/Entity.hpp"
#include "entities/Resource.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "utils/ResourceHandle.hpp"
#include "utils/Vector2D.hpp"

using HammerEngine::ResourceHandle;

// Simple mock entity for testing
class MockEntity : public Entity {
public:
  MockEntity(const std::string &id) : m_id(id) {}

  void update(float deltaTime) override { (void)deltaTime; }
  void render() override {  }
  void clean() override {}

  std::string getId() const { return m_id; }

private:
  std::string m_id;
};

class ResourceChangeEventTestFixture {
public:
  ResourceChangeEventTestFixture() {
    // Initialize ResourceTemplateManager
    resourceManager = &ResourceTemplateManager::Instance();
    resourceManager->init();

    // Create simple mock entities for testing
    player = std::make_shared<MockEntity>("test_player");
    npc = std::make_shared<MockEntity>("test_npc");

    // Create test resource handles through ResourceTemplateManager
    auto healthPotion = Resource::create<Resource>(
        resourceManager->generateHandle(), "test_health_potion",
        "Test Health Potion", ResourceCategory::Item, ResourceType::Consumable);
    auto ironSword = Resource::create<Resource>(
        resourceManager->generateHandle(), "test_iron_sword", "Test Iron Sword",
        ResourceCategory::Item, ResourceType::Equipment);

    resourceManager->registerResourceTemplate(healthPotion);
    resourceManager->registerResourceTemplate(ironSword);

    healthPotionHandle = healthPotion->getHandle();
    ironSwordHandle = ironSword->getHandle();
  }

  ~ResourceChangeEventTestFixture() {
    // Explicitly clean up before singletons are destroyed to prevent crashes
    player.reset();
    npc.reset();

    // Clean up the ResourceTemplateManager to prevent shutdown issues
    if (resourceManager && resourceManager->isInitialized()) {
      resourceManager->clean();
    }
  }

protected:
  ResourceTemplateManager *resourceManager;
  std::shared_ptr<MockEntity> player;
  std::shared_ptr<MockEntity> npc;
  HammerEngine::ResourceHandle healthPotionHandle;
  HammerEngine::ResourceHandle ironSwordHandle;
};

BOOST_FIXTURE_TEST_SUITE(ResourceChangeEventTestSuite,
                         ResourceChangeEventTestFixture)

BOOST_AUTO_TEST_CASE(TestResourceChangeEventCreation) {
  // Test creating a ResourceChangeEvent with all parameters
  ResourceChangeEvent event(player, healthPotionHandle, 5, 10, "crafted");

  // Test basic getters
  auto ownerPtr = event.getOwner().lock();
  BOOST_REQUIRE(ownerPtr != nullptr);
  BOOST_CHECK_EQUAL(ownerPtr.get(), player.get());
  BOOST_CHECK(event.getResourceHandle() == healthPotionHandle);
  BOOST_CHECK_EQUAL(event.getOldQuantity(), 5);
  BOOST_CHECK_EQUAL(event.getNewQuantity(), 10);
  BOOST_CHECK_EQUAL(event.getQuantityChange(), 5);
  BOOST_CHECK_EQUAL(event.getChangeReason(), "crafted");
}

BOOST_AUTO_TEST_CASE(TestResourceChangeEventTypes) {
  // Test different types of quantity changes

  // Test addition (increase)
  ResourceChangeEvent addedEvent(player, healthPotionHandle, 3, 8, "found");
  BOOST_CHECK(addedEvent.isIncrease());
  BOOST_CHECK(!addedEvent.isDecrease());
  BOOST_CHECK_EQUAL(addedEvent.getQuantityChange(), 5);

  // Test removal (decrease)
  ResourceChangeEvent removedEvent(npc, ironSwordHandle, 10, 3, "consumed");
  BOOST_CHECK(!removedEvent.isIncrease());
  BOOST_CHECK(removedEvent.isDecrease());
  BOOST_CHECK_EQUAL(removedEvent.getQuantityChange(), -7);

  // Test new resource (0 to positive)
  auto newItemHandle = resourceManager->generateHandle();
  ResourceChangeEvent newResourceEvent(player, newItemHandle, 0, 5, "acquired");
  BOOST_CHECK(newResourceEvent.isResourceAdded());
  BOOST_CHECK(!newResourceEvent.isResourceRemoved());
  BOOST_CHECK(newResourceEvent.isIncrease());

  // Test resource removal (positive to 0)
  auto oldItemHandle = resourceManager->generateHandle();
  ResourceChangeEvent resourceRemovedEvent(npc, oldItemHandle, 3, 0, "lost");
  BOOST_CHECK(!resourceRemovedEvent.isResourceAdded());
  BOOST_CHECK(resourceRemovedEvent.isResourceRemoved());
  BOOST_CHECK(resourceRemovedEvent.isDecrease());
}

BOOST_AUTO_TEST_CASE(TestQuantityCalculations) {
  // Test quantity change calculations
  ResourceChangeEvent increaseEvent(player, healthPotionHandle, 10, 25,
                                    "bought");
  BOOST_CHECK_EQUAL(increaseEvent.getQuantityChange(), 15);
  BOOST_CHECK(increaseEvent.isIncrease());

  ResourceChangeEvent decreaseEvent(npc, ironSwordHandle, 20, 8, "used");
  BOOST_CHECK_EQUAL(decreaseEvent.getQuantityChange(), -12);
  BOOST_CHECK(decreaseEvent.isDecrease());

  auto stableItemHandle = resourceManager->generateHandle();
  ResourceChangeEvent noChangeEvent(player, stableItemHandle, 5, 5, "checked");
  BOOST_CHECK_EQUAL(noChangeEvent.getQuantityChange(), 0);
  BOOST_CHECK(!noChangeEvent.isIncrease());
  BOOST_CHECK(!noChangeEvent.isDecrease());
}

BOOST_AUTO_TEST_CASE(TestEventInterface) {
  // Test Event interface implementation
  ResourceChangeEvent event(player, healthPotionHandle, 0, 5, "initial");

  // Test event methods
  BOOST_CHECK_EQUAL(event.getName(), "ResourceChange");
  BOOST_CHECK_EQUAL(event.getType(), ResourceChangeEvent::EVENT_TYPE);
  BOOST_CHECK(event.checkConditions());

  // Test that these don't throw (no-op implementations)
  BOOST_CHECK_NO_THROW(event.update());
  BOOST_CHECK_NO_THROW(event.execute());
  BOOST_CHECK_NO_THROW(event.reset());
  BOOST_CHECK_NO_THROW(event.clean());
}

BOOST_AUTO_TEST_CASE(TestEntityOwnership) {
  // Test that entity ownership works correctly
  ResourceChangeEvent playerEvent(player, healthPotionHandle, 1, 3,
                                  "player_action");
  auto playerOwner = playerEvent.getOwner().lock();
  BOOST_REQUIRE(playerOwner != nullptr);
  BOOST_CHECK_EQUAL(playerOwner.get(), player.get());

  ResourceChangeEvent npcEvent(npc, ironSwordHandle, 2, 1, "npc_action");
  auto npcOwner = npcEvent.getOwner().lock();
  BOOST_REQUIRE(npcOwner != nullptr);
  BOOST_CHECK_EQUAL(npcOwner.get(), npc.get());

  // Test that events reference different entities
  BOOST_CHECK(playerOwner.get() != npcOwner.get());
}

BOOST_AUTO_TEST_CASE(TestResourceIdentification) {
  // Test that resource handles are correctly stored and retrieved
  ResourceChangeEvent healthEvent(player, healthPotionHandle, 0, 3, "healed");
  BOOST_CHECK(healthEvent.getResourceHandle() == healthPotionHandle);

  ResourceChangeEvent swordEvent(npc, ironSwordHandle, 1, 0, "broke");
  BOOST_CHECK(swordEvent.getResourceHandle() == ironSwordHandle);

  // Test different resource handles
  auto customResourceHandle = resourceManager->generateHandle();
  ResourceChangeEvent customEvent(player, customResourceHandle, 5, 15,
                                  "custom");
  BOOST_CHECK(customEvent.getResourceHandle() == customResourceHandle);
}

BOOST_AUTO_TEST_CASE(TestChangeReasons) {
  // Test different change reasons
  std::vector<std::string> reasons = {
      "crafted", "bought", "sold",   "consumed", "dropped",
      "found",   "traded", "gifted", "stolen",   "repaired"};

  for (const auto &reason : reasons) {
    ResourceChangeEvent event(player, healthPotionHandle, 1, 2, reason);
    BOOST_CHECK_EQUAL(event.getChangeReason(), reason);
  }

  // Test empty reason
  ResourceChangeEvent noReasonEvent(player, healthPotionHandle, 1, 2);
  BOOST_CHECK_EQUAL(noReasonEvent.getChangeReason(), "");
}

BOOST_AUTO_TEST_CASE(TestEdgeCases) {
  // Test zero quantities
  auto emptyResourceHandle = resourceManager->generateHandle();
  ResourceChangeEvent zeroToZeroEvent(player, emptyResourceHandle, 0, 0,
                                      "no_change");
  BOOST_CHECK_EQUAL(zeroToZeroEvent.getQuantityChange(), 0);
  BOOST_CHECK(!zeroToZeroEvent.isIncrease());
  BOOST_CHECK(!zeroToZeroEvent.isDecrease());
  BOOST_CHECK(!zeroToZeroEvent.isResourceAdded());
  BOOST_CHECK(!zeroToZeroEvent.isResourceRemoved());

  // Test large quantities
  auto bulkItemHandle = resourceManager->generateHandle();
  ResourceChangeEvent largeEvent(npc, bulkItemHandle, 10000, 50000,
                                 "bulk_operation");
  BOOST_CHECK_EQUAL(largeEvent.getQuantityChange(), 40000);
  BOOST_CHECK(largeEvent.isIncrease());

  // Test negative change (large decrease)
  auto depletedResourceHandle = resourceManager->generateHandle();
  ResourceChangeEvent massiveDecreaseEvent(player, depletedResourceHandle,
                                           100000, 1, "massive_use");
  BOOST_CHECK_EQUAL(massiveDecreaseEvent.getQuantityChange(), -99999);
  BOOST_CHECK(massiveDecreaseEvent.isDecrease());
}

BOOST_AUTO_TEST_CASE(TestEventStaticType) {
  // Test that EVENT_TYPE static member is accessible
  BOOST_CHECK(!ResourceChangeEvent::EVENT_TYPE.empty());

  // Test that instance getType() matches static EVENT_TYPE
  ResourceChangeEvent event(player, healthPotionHandle, 1, 2, "test");
  BOOST_CHECK_EQUAL(event.getType(), ResourceChangeEvent::EVENT_TYPE);
}

BOOST_AUTO_TEST_SUITE_END()