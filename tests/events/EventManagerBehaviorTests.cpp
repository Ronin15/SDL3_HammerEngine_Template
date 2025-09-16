/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE EventManagerBehaviorTests
#include <boost/test/unit_test.hpp>

#include "EventManagerTestAccess.hpp"
#include "core/ThreadSystem.hpp"
#include "events/CameraEvent.hpp"
#include "events/Event.hpp"
#include "managers/EventManager.hpp"

namespace {

class TestEvent : public Event {
public:
  explicit TestEvent(const std::string &name) : m_name(name) {}
  void update() override {}
  void execute() override { ++m_executeCount; }
  void reset() override { m_executeCount = 0; }
  void clean() override {}
  std::string getName() const override { return m_name; }
  std::string getType() const override { return "Custom"; }
  std::string getTypeName() const override { return "TestEvent"; }
  EventTypeId getTypeId() const override { return EventTypeId::Custom; }
  bool checkConditions() override { return true; }

  int getExecuteCount() const { return m_executeCount; }

private:
  std::string m_name;
  int m_executeCount{0};
};

struct EventFixture {
  EventFixture() {
    HammerEngine::ThreadSystem::Instance().init();
    EventManagerTestAccess::reset();
  }
  ~EventFixture() {
    // Clean after each test
    EventManager::Instance().clean();
    HammerEngine::ThreadSystem::Instance().clean();
  }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(EventBehaviorSuite, EventFixture)

BOOST_AUTO_TEST_CASE(ExecuteEvent_NoHandlers_ExecutesDirectly) {
  auto e = std::make_shared<TestEvent>("TestA");
  EventManager::Instance().registerEvent("TestA", e);
  BOOST_CHECK(EventManager::Instance().executeEvent("TestA"));
  BOOST_CHECK_EQUAL(e->getExecuteCount(), 1);
}

BOOST_AUTO_TEST_CASE(ExecuteEvent_WithHandlers_DoesNotAutoExecute) {
  auto e = std::make_shared<TestEvent>("TestB");
  EventManager::Instance().registerEvent("TestB", e);

  // Register a handler for Custom type that does NOT call execute()
  auto tok = EventManager::Instance().registerHandlerWithToken(
      EventTypeId::Custom, [](const EventData &) {});
  (void)tok;

  BOOST_CHECK(EventManager::Instance().executeEvent("TestB"));
  BOOST_CHECK_EQUAL(e->getExecuteCount(), 0); // Not auto-executed
}

BOOST_AUTO_TEST_CASE(ChangeWeather_FallbackWithoutHandlers) {
  // No handlers registered for Weather
  bool ok = EventManager::Instance().changeWeather("Rainy", 1.0f);
  BOOST_CHECK(ok);
}

BOOST_AUTO_TEST_CASE(SpawnNPC_FallbackWithoutHandlers) {
  bool ok = EventManager::Instance().spawnNPC("Guard", 10.0f, 20.0f);
  BOOST_CHECK(ok);
}

BOOST_AUTO_TEST_CASE(TriggerParticleEffect_FallbackWithoutHandlers) {
  bool ok =
      EventManager::Instance().triggerParticleEffect("Fire", 100.0f, 200.0f);
  BOOST_CHECK(ok);
}

BOOST_AUTO_TEST_CASE(RegisterCameraEvent_StoresEvent) {
  auto camEvent =
      std::make_shared<CameraMovedEvent>(Vector2D(10, 10), Vector2D(0, 0));
  BOOST_CHECK(
      EventManager::Instance().registerCameraEvent("cam_move_test", camEvent));
  auto stored = EventManager::Instance().getEvent("cam_move_test");
  BOOST_CHECK(stored != nullptr);
}

BOOST_AUTO_TEST_CASE(RemoveNameHandlers_RemovesHandlers) {
  // Register a per-name handler
  EventManager::Instance().registerHandlerForName(
      "TestName", [](const EventData &) {
        BOOST_CHECK_MESSAGE(false, "Handler should have been removed");
      });

  // Remove it
  EventManager::Instance().removeNameHandlers("TestName");

  // Trigger the name (register a dummy event and execute by name)
  auto e = std::make_shared<TestEvent>("TestName");
  EventManager::Instance().registerEvent("TestName", e);
  // Should not hit the above failing handler
  BOOST_CHECK(EventManager::Instance().executeEvent("TestName"));
}

BOOST_AUTO_TEST_SUITE_END()
