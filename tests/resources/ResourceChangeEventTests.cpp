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
#include "managers/ResourceManager.hpp"
#include "utils/Vector2D.hpp"

// Simple mock entity for testing
class MockEntity : public Entity {
public:
  MockEntity(const std::string &id) : m_id(id) {}

  void update(float deltaTime) override { (void)deltaTime; }
  void render() override {}
  void clean() override {}

  std::string getId() const { return m_id; }

private:
  std::string m_id;
};

class ResourceChangeEventTestFixture {
public:
  ResourceChangeEventTestFixture() {
    // Initialize ResourceManager
    resourceManager = &ResourceManager::Instance();

    // Create simple mock entities for testing
    player = std::make_shared<MockEntity>("test_player");
    npc = std::make_shared<MockEntity>("test_npc");

    // Test resource IDs
    healthPotionId = "health_potion";
    ironSwordId = "iron_sword";
  }

protected:
  ResourceManager *resourceManager;
  std::shared_ptr<MockEntity> player;
  std::shared_ptr<MockEntity> npc;
  std::string healthPotionId;
  std::string ironSwordId;
};

BOOST_FIXTURE_TEST_SUITE(ResourceChangeEventTestSuite,
                         ResourceChangeEventTestFixture)

BOOST_AUTO_TEST_CASE(TestResourceChangeEventCreation) {
  // Test creating a ResourceChangeEvent with all parameters
  ResourceChangeEvent event(player, healthPotionId, 5, 10, "crafted");

  // Test basic getters
  auto ownerPtr = event.getOwner().lock();
  BOOST_REQUIRE(ownerPtr != nullptr);
  BOOST_CHECK_EQUAL(ownerPtr.get(), player.get());
  BOOST_CHECK_EQUAL(event.getResourceId(), healthPotionId);
  BOOST_CHECK_EQUAL(event.getOldQuantity(), 5);
  BOOST_CHECK_EQUAL(event.getNewQuantity(), 10);
  BOOST_CHECK_EQUAL(event.getQuantityChange(), 5);
  BOOST_CHECK_EQUAL(event.getChangeReason(), "crafted");
}

BOOST_AUTO_TEST_CASE(TestResourceChangeEventTypes) {
  // Test different types of quantity changes

  // Test addition (increase)
  ResourceChangeEvent addedEvent(player, healthPotionId, 3, 8, "found");
  BOOST_CHECK(addedEvent.isIncrease());
  BOOST_CHECK(!addedEvent.isDecrease());
  BOOST_CHECK_EQUAL(addedEvent.getQuantityChange(), 5);

  // Test removal (decrease)
  ResourceChangeEvent removedEvent(npc, ironSwordId, 10, 3, "consumed");
  BOOST_CHECK(!removedEvent.isIncrease());
  BOOST_CHECK(removedEvent.isDecrease());
  BOOST_CHECK_EQUAL(removedEvent.getQuantityChange(), -7);

  // Test new resource (0 to positive)
  ResourceChangeEvent newResourceEvent(player, "new_item", 0, 5, "acquired");
  BOOST_CHECK(newResourceEvent.isResourceAdded());
  BOOST_CHECK(!newResourceEvent.isResourceRemoved());
  BOOST_CHECK(newResourceEvent.isIncrease());

  // Test resource removal (positive to 0)
  ResourceChangeEvent resourceRemovedEvent(npc, "old_item", 3, 0, "lost");
  BOOST_CHECK(!resourceRemovedEvent.isResourceAdded());
  BOOST_CHECK(resourceRemovedEvent.isResourceRemoved());
  BOOST_CHECK(resourceRemovedEvent.isDecrease());
}

BOOST_AUTO_TEST_CASE(TestQuantityCalculations) {
  // Test quantity change calculations
  ResourceChangeEvent increaseEvent(player, healthPotionId, 10, 25, "bought");
  BOOST_CHECK_EQUAL(increaseEvent.getQuantityChange(), 15);
  BOOST_CHECK(increaseEvent.isIncrease());

  ResourceChangeEvent decreaseEvent(npc, ironSwordId, 20, 8, "used");
  BOOST_CHECK_EQUAL(decreaseEvent.getQuantityChange(), -12);
  BOOST_CHECK(decreaseEvent.isDecrease());

  ResourceChangeEvent noChangeEvent(player, "stable_item", 5, 5, "checked");
  BOOST_CHECK_EQUAL(noChangeEvent.getQuantityChange(), 0);
  BOOST_CHECK(!noChangeEvent.isIncrease());
  BOOST_CHECK(!noChangeEvent.isDecrease());
}

BOOST_AUTO_TEST_CASE(TestEventInterface) {
  // Test Event interface implementation
  ResourceChangeEvent event(player, healthPotionId, 0, 5, "initial");

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
  ResourceChangeEvent playerEvent(player, healthPotionId, 1, 3,
                                  "player_action");
  auto playerOwner = playerEvent.getOwner().lock();
  BOOST_REQUIRE(playerOwner != nullptr);
  BOOST_CHECK_EQUAL(playerOwner.get(), player.get());

  ResourceChangeEvent npcEvent(npc, ironSwordId, 2, 1, "npc_action");
  auto npcOwner = npcEvent.getOwner().lock();
  BOOST_REQUIRE(npcOwner != nullptr);
  BOOST_CHECK_EQUAL(npcOwner.get(), npc.get());

  // Test that events reference different entities
  BOOST_CHECK(playerOwner.get() != npcOwner.get());
}

BOOST_AUTO_TEST_CASE(TestResourceIdentification) {
  // Test that resource IDs are correctly stored and retrieved
  ResourceChangeEvent healthEvent(player, healthPotionId, 0, 3, "healed");
  BOOST_CHECK_EQUAL(healthEvent.getResourceId(), healthPotionId);

  ResourceChangeEvent swordEvent(npc, ironSwordId, 1, 0, "broke");
  BOOST_CHECK_EQUAL(swordEvent.getResourceId(), ironSwordId);

  // Test different resource types
  std::string customResourceId = "custom_resource_123";
  ResourceChangeEvent customEvent(player, customResourceId, 5, 15, "custom");
  BOOST_CHECK_EQUAL(customEvent.getResourceId(), customResourceId);
}

BOOST_AUTO_TEST_CASE(TestChangeReasons) {
  // Test different change reasons
  std::vector<std::string> reasons = {
      "crafted", "bought", "sold",   "consumed", "dropped",
      "found",   "traded", "gifted", "stolen",   "repaired"};

  for (const auto &reason : reasons) {
    ResourceChangeEvent event(player, healthPotionId, 1, 2, reason);
    BOOST_CHECK_EQUAL(event.getChangeReason(), reason);
  }

  // Test empty reason
  ResourceChangeEvent noReasonEvent(player, healthPotionId, 1, 2);
  BOOST_CHECK_EQUAL(noReasonEvent.getChangeReason(), "");
}

BOOST_AUTO_TEST_CASE(TestEdgeCases) {
  // Test zero quantities
  ResourceChangeEvent zeroToZeroEvent(player, "empty_resource", 0, 0,
                                      "no_change");
  BOOST_CHECK_EQUAL(zeroToZeroEvent.getQuantityChange(), 0);
  BOOST_CHECK(!zeroToZeroEvent.isIncrease());
  BOOST_CHECK(!zeroToZeroEvent.isDecrease());
  BOOST_CHECK(!zeroToZeroEvent.isResourceAdded());
  BOOST_CHECK(!zeroToZeroEvent.isResourceRemoved());

  // Test large quantities
  ResourceChangeEvent largeEvent(npc, "bulk_item", 10000, 50000,
                                 "bulk_operation");
  BOOST_CHECK_EQUAL(largeEvent.getQuantityChange(), 40000);
  BOOST_CHECK(largeEvent.isIncrease());

  // Test negative change (large decrease)
  ResourceChangeEvent massiveDecreaseEvent(player, "depleted_resource", 100000,
                                           1, "massive_use");
  BOOST_CHECK_EQUAL(massiveDecreaseEvent.getQuantityChange(), -99999);
  BOOST_CHECK(massiveDecreaseEvent.isDecrease());
}

BOOST_AUTO_TEST_CASE(TestEventStaticType) {
  // Test that EVENT_TYPE static member is accessible
  BOOST_CHECK(!ResourceChangeEvent::EVENT_TYPE.empty());

  // Test that instance getType() matches static EVENT_TYPE
  ResourceChangeEvent event(player, healthPotionId, 1, 2, "test");
  BOOST_CHECK_EQUAL(event.getType(), ResourceChangeEvent::EVENT_TYPE);
}

BOOST_AUTO_TEST_SUITE_END()