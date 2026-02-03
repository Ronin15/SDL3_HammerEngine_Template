/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE EventManagerTest
// BOOST_TEST_NO_SIGNAL_HANDLING is already defined on the command line
#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "entities/EntityHandle.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "collisions/CollisionInfo.hpp"
#include "events/Event.hpp"
#include "events/ParticleEffectEvent.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "events/WeatherEvent.hpp"
#include "managers/EventManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "utils/ResourceHandle.hpp"
#include "EventManagerTestAccess.hpp"

// Test handle for ResourceChangeEvent tests - no real entity needed
static const EntityHandle TEST_PLAYER_HANDLE{1, EntityKind::Player, 1};

// Mock Event class for testing
class MockEvent : public Event {
public:
  MockEvent(const std::string &name)
      : m_name(name), m_executed(false), m_conditionsMet(false) {}

  void update() override { m_updated = true; }

  void execute() override { m_executed = true; }

  void reset() override {
    m_updated = false;
    m_executed = false;
    m_conditionsMet = false;
  }

  void clean() override {}

  std::string getName() const override { return m_name; }
  std::string getType() const override { return "Mock"; }
  std::string getTypeName() const override { return "MockEvent"; }
  EventTypeId getTypeId() const override { return EventTypeId::Custom; }

  bool checkConditions() override { return m_conditionsMet; }

  void setConditionsMet(bool met) { m_conditionsMet = met; }
  bool wasExecuted() const { return m_executed; }
  bool wasUpdated() const { return m_updated; }

private:
  std::string m_name;
  bool m_executed{false};
  bool m_updated{false};
  bool m_conditionsMet{false};
};

// Global fixture to initialize ThreadSystem and EntityDataManager once for all tests
struct GlobalEventTestFixture {
  GlobalEventTestFixture() {
    // Initialize ThreadSystem once for all tests
    if (!HammerEngine::ThreadSystem::Exists()) {
      HammerEngine::ThreadSystem::Instance().init();
    }
    // Initialize EntityDataManager (required for Player entity creation in DOD)
    EntityDataManager::Instance().init();
    // Ensure benchmark mode is disabled for regular tests
    HAMMER_DISABLE_BENCHMARK_MODE();
  }

  ~GlobalEventTestFixture() {
    // Clean up EntityDataManager
    EntityDataManager::Instance().clean();
    // Clean up ThreadSystem at the very end
    if (HammerEngine::ThreadSystem::Exists()) {
      HammerEngine::ThreadSystem::Instance().clean();
    }
  }
};

BOOST_GLOBAL_FIXTURE(GlobalEventTestFixture);

struct EventManagerFixture {
  EventManagerFixture() {
    // Don't reinitialize ThreadSystem - use the global one
    EventManagerTestAccess::reset();
  }

  ~EventManagerFixture() {
    // Disable threading before cleanup
    #ifndef NDEBUG
    EventManager::Instance().enableThreading(false);
    #endif
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Clean up the EventManager
    EventManager::Instance().clean();
  }
};

// ==================== Basic Initialization Tests ====================

BOOST_FIXTURE_TEST_CASE(InitAndClean, EventManagerFixture) {
  BOOST_CHECK(EventManager::Instance().init());
  BOOST_CHECK(EventManager::Instance().isInitialized());
  EventManager::Instance().clean();
}

// ==================== Dispatch Architecture Tests ====================

BOOST_FIXTURE_TEST_CASE(DispatchEvent_WithHandler_CallsHandler, EventManagerFixture) {
  auto mockEvent = std::make_shared<MockEvent>("TestEvent");

  std::atomic<bool> handlerCalled{false};
  auto token = EventManager::Instance().registerHandlerWithToken(
      EventTypeId::Custom, [&handlerCalled](const EventData &data) {
        if (data.isActive()) handlerCalled.store(true);
      });

  // Dispatch the event
  BOOST_CHECK(EventManager::Instance().dispatchEvent(mockEvent));
  EventManager::Instance().update(); // Process deferred events

  BOOST_CHECK(handlerCalled.load());
  EventManager::Instance().removeHandler(token);
}

BOOST_FIXTURE_TEST_CASE(DispatchEvent_ImmediateMode_CallsHandlerSynchronously, EventManagerFixture) {
  auto mockEvent = std::make_shared<MockEvent>("TestEvent");

  std::atomic<bool> handlerCalled{false};
  auto token = EventManager::Instance().registerHandlerWithToken(
      EventTypeId::Custom, [&handlerCalled](const EventData &data) {
        if (data.isActive()) handlerCalled.store(true);
      });

  // Dispatch with Immediate mode - should call handler before returning
  EventManager::Instance().dispatchEvent(mockEvent, EventManager::DispatchMode::Immediate);

  // Handler should already be called (no update() needed)
  BOOST_CHECK(handlerCalled.load());
  EventManager::Instance().removeHandler(token);
}

BOOST_FIXTURE_TEST_CASE(DispatchEvent_DeferredMode_RequiresUpdate, EventManagerFixture) {
  auto mockEvent = std::make_shared<MockEvent>("TestEvent");

  std::atomic<bool> handlerCalled{false};
  auto token = EventManager::Instance().registerHandlerWithToken(
      EventTypeId::Custom, [&handlerCalled](const EventData &data) {
        if (data.isActive()) handlerCalled.store(true);
      });

  // Dispatch with Deferred mode (default)
  EventManager::Instance().dispatchEvent(mockEvent, EventManager::DispatchMode::Deferred);

  // Handler should NOT be called yet
  BOOST_CHECK(!handlerCalled.load());

  // Now process deferred events
  EventManager::Instance().update();

  // Handler should now be called
  BOOST_CHECK(handlerCalled.load());
  EventManager::Instance().removeHandler(token);
}

// ==================== Handler Registration Tests ====================

BOOST_FIXTURE_TEST_CASE(RegisterHandlerWithToken_CanBeRemoved, EventManagerFixture) {
  std::atomic<int> callCount{0};

  auto token = EventManager::Instance().registerHandlerWithToken(
      EventTypeId::Custom,
      [&callCount](const EventData &data) {
        if (data.isActive()) ++callCount;
      });

  // Dispatch once - handler should be called
  auto e1 = std::make_shared<MockEvent>("Test1");
  EventManager::Instance().dispatchEvent(e1, EventManager::DispatchMode::Immediate);
  BOOST_CHECK_EQUAL(callCount.load(), 1);

  // Remove handler
  EventManager::Instance().removeHandler(token);

  // Dispatch again - handler should NOT be called
  auto e2 = std::make_shared<MockEvent>("Test2");
  EventManager::Instance().dispatchEvent(e2, EventManager::DispatchMode::Immediate);
  BOOST_CHECK_EQUAL(callCount.load(), 1); // Still 1, not incremented
}

BOOST_FIXTURE_TEST_CASE(RegisterNameHandler_CanBeRemoved, EventManagerFixture) {
  std::atomic<bool> handlerCalled{false};

  // Register a per-name handler
  EventManager::Instance().registerHandlerForName(
      "TestName", [&handlerCalled](const EventData &) {
        handlerCalled.store(true);
      });

  // Remove it
  EventManager::Instance().removeNameHandlers("TestName");

  // Dispatch an event with that name - handler should not be called
  auto e = std::make_shared<MockEvent>("TestName");
  EventManager::Instance().dispatchEvent(e, EventManager::DispatchMode::Immediate);

  BOOST_CHECK(!handlerCalled.load()); // Handler was removed
}

// ==================== Trigger Method Tests ====================

BOOST_FIXTURE_TEST_CASE(ChangeWeather_DispatchesToHandlers, EventManagerFixture) {
  std::atomic<bool> weatherHandlerCalled{false};
  auto tok = EventManager::Instance().registerHandlerWithToken(
      EventTypeId::Weather, [&weatherHandlerCalled](const EventData &data) {
        if (data.event) weatherHandlerCalled.store(true);
      });

  bool ok = EventManager::Instance().changeWeather("Rainy", 1.0f,
                                                    EventManager::DispatchMode::Immediate);
  BOOST_CHECK(ok);
  BOOST_CHECK(weatherHandlerCalled.load());

  EventManager::Instance().removeHandler(tok);
}

BOOST_FIXTURE_TEST_CASE(SpawnNPC_DispatchesToHandlers, EventManagerFixture) {
  std::atomic<bool> npcHandlerCalled{false};
  auto tok = EventManager::Instance().registerHandlerWithToken(
      EventTypeId::NPCSpawn, [&npcHandlerCalled](const EventData &data) {
        if (data.event) npcHandlerCalled.store(true);
      });

  bool ok = EventManager::Instance().spawnNPC("Guard", 10.0f, 20.0f, 1, 0.0f, "",
                                               {}, false,
                                               EventManager::DispatchMode::Immediate);
  BOOST_CHECK(ok);
  BOOST_CHECK(npcHandlerCalled.load());

  EventManager::Instance().removeHandler(tok);
}

BOOST_FIXTURE_TEST_CASE(TriggerParticleEffect_DispatchesToHandlers, EventManagerFixture) {
  std::atomic<bool> particleHandlerCalled{false};
  auto tok = EventManager::Instance().registerHandlerWithToken(
      EventTypeId::ParticleEffect, [&particleHandlerCalled](const EventData &data) {
        if (data.event) particleHandlerCalled.store(true);
      });

  bool ok = EventManager::Instance().triggerParticleEffect("Fire", 100.0f, 200.0f,
                                                            1.0f, -1.0f, "",
                                                            EventManager::DispatchMode::Immediate);
  BOOST_CHECK(ok);
  BOOST_CHECK(particleHandlerCalled.load());

  EventManager::Instance().removeHandler(tok);
}

BOOST_FIXTURE_TEST_CASE(TriggerResourceChange_DispatchesToHandlers, EventManagerFixture) {
  std::atomic<bool> resourceHandlerCalled{false};
  auto tok = EventManager::Instance().registerHandlerWithToken(
      EventTypeId::ResourceChange, [&resourceHandlerCalled](const EventData &data) {
        if (data.event) resourceHandlerCalled.store(true);
      });

  HammerEngine::ResourceHandle testResource(1, 1);
  bool ok = EventManager::Instance().triggerResourceChange(
      TEST_PLAYER_HANDLE, testResource, 5, 10, "test",
      EventManager::DispatchMode::Immediate);
  BOOST_CHECK(ok);
  BOOST_CHECK(resourceHandlerCalled.load());

  EventManager::Instance().removeHandler(tok);
}

BOOST_FIXTURE_TEST_CASE(TriggerCollision_DispatchesToHandlers, EventManagerFixture) {
  std::atomic<bool> collisionHandlerCalled{false};
  auto tok = EventManager::Instance().registerHandlerWithToken(
      EventTypeId::Collision, [&collisionHandlerCalled](const EventData &data) {
        if (data.event) collisionHandlerCalled.store(true);
      });

  HammerEngine::CollisionInfo info{};
  bool ok = EventManager::Instance().triggerCollision(info,
                                                       EventManager::DispatchMode::Immediate);
  BOOST_CHECK(ok);
  BOOST_CHECK(collisionHandlerCalled.load());

  EventManager::Instance().removeHandler(tok);
}

BOOST_FIXTURE_TEST_CASE(TriggerWorldLoaded_DispatchesToHandlers, EventManagerFixture) {
  std::atomic<bool> worldHandlerCalled{false};
  auto tok = EventManager::Instance().registerHandlerWithToken(
      EventTypeId::World, [&worldHandlerCalled](const EventData &data) {
        if (data.event) worldHandlerCalled.store(true);
      });

  bool ok = EventManager::Instance().triggerWorldLoaded("test_world", 100, 100,
                                                         EventManager::DispatchMode::Immediate);
  BOOST_CHECK(ok);
  BOOST_CHECK(worldHandlerCalled.load());

  EventManager::Instance().removeHandler(tok);
}

BOOST_FIXTURE_TEST_CASE(TriggerCameraMoved_DispatchesToHandlers, EventManagerFixture) {
  std::atomic<bool> cameraHandlerCalled{false};
  auto tok = EventManager::Instance().registerHandlerWithToken(
      EventTypeId::Camera, [&cameraHandlerCalled](const EventData &data) {
        if (data.event) cameraHandlerCalled.store(true);
      });

  bool ok = EventManager::Instance().triggerCameraMoved(
      Vector2D(100, 100), Vector2D(0, 0),
      EventManager::DispatchMode::Immediate);
  BOOST_CHECK(ok);
  BOOST_CHECK(cameraHandlerCalled.load());

  EventManager::Instance().removeHandler(tok);
}

// ==================== Deferred Dispatch Tests ====================

BOOST_FIXTURE_TEST_CASE(DeferredDispatch_MultipleTriggers_ProcessedInUpdate, EventManagerFixture) {
  std::atomic<int> handlerCallCount{0};

  EventManager::Instance().registerHandler(
      EventTypeId::Weather,
      [&handlerCallCount](const EventData &) { handlerCallCount.fetch_add(1); });
  EventManager::Instance().registerHandler(
      EventTypeId::NPCSpawn,
      [&handlerCallCount](const EventData &) { handlerCallCount.fetch_add(1); });

  // Trigger multiple events with deferred dispatch (default)
  BOOST_CHECK(EventManager::Instance().changeWeather("Storm", 3.0f));
  BOOST_CHECK(EventManager::Instance().spawnNPC("Boss", 500.0f, 300.0f));

  // Events should be queued, handlers not called yet
  BOOST_CHECK_EQUAL(handlerCallCount.load(), 0);

  // Process deferred events
  EventManager::Instance().update();

  // Allow processing time
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Handlers should now be called
  BOOST_CHECK_GE(handlerCallCount.load(), 2);
}

// ==================== Performance Stats Tests ====================

BOOST_FIXTURE_TEST_CASE(PerformanceStats_TrackDispatchMetrics, EventManagerFixture) {
  // Reset performance stats
  EventManager::Instance().resetPerformanceStats();

  // Register a handler
  bool handlerCalled = false;
  EventManager::Instance().registerHandler(
      EventTypeId::Weather,
      [&handlerCalled](const EventData &) { handlerCalled = true; });

  // Trigger an event with immediate dispatch
  BOOST_CHECK(EventManager::Instance().changeWeather("Sunny", 1.0f,
                                                      EventManager::DispatchMode::Immediate));
  BOOST_CHECK(handlerCalled);

  // Get performance stats
  auto stats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);

  // Verify basic stats structure exists
  BOOST_CHECK_GE(stats.callCount, 0);
  BOOST_CHECK_GE(stats.totalTime, 0.0);
  BOOST_CHECK_GE(stats.avgTime, 0.0);
}

BOOST_FIXTURE_TEST_CASE(GetPendingEventCount_TracksQueuedEvents, EventManagerFixture) {
  // Initially should have no pending events
  BOOST_CHECK_EQUAL(EventManager::Instance().getPendingEventCount(), 0);

  // Queue some deferred events
  EventManager::Instance().changeWeather("Rainy", 1.0f);
  EventManager::Instance().spawnNPC("Guard", 0, 0);

  // Should have pending events
  BOOST_CHECK_GT(EventManager::Instance().getPendingEventCount(), 0);

  // Process events
  EventManager::Instance().update();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Should have no pending events after processing
  BOOST_CHECK_EQUAL(EventManager::Instance().getPendingEventCount(), 0);
}

// ==================== Event Pool Recycling Tests ====================

BOOST_FIXTURE_TEST_CASE(EventPoolRecycling_WeatherEvents, EventManagerFixture) {
  EventPtr firstWeather = nullptr;
  EventPtr secondWeather = nullptr;
  EventManager::Instance().registerHandler(
      EventTypeId::Weather, [&](const EventData &data) {
        if (!firstWeather) firstWeather = data.event;
        else secondWeather = data.event;
      });

  EventManager::Instance().changeWeather("Storm", 1.0f, EventManager::DispatchMode::Immediate);
  EventManager::Instance().changeWeather("Clear", 1.0f, EventManager::DispatchMode::Immediate);

  BOOST_CHECK(firstWeather != nullptr);
  BOOST_CHECK(secondWeather != nullptr);
  BOOST_CHECK_EQUAL(firstWeather.get(), secondWeather.get()); // Same pooled event reused
}

BOOST_FIXTURE_TEST_CASE(EventPoolRecycling_NPCSpawnEvents, EventManagerFixture) {
  EventPtr firstNPC = nullptr;
  EventPtr secondNPC = nullptr;
  EventManager::Instance().registerHandler(
      EventTypeId::NPCSpawn, [&](const EventData &data) {
        if (!firstNPC) firstNPC = data.event;
        else secondNPC = data.event;
      });

  EventManager::Instance().spawnNPC("A", 0, 0, 1, 0, "", {}, false, EventManager::DispatchMode::Immediate);
  EventManager::Instance().spawnNPC("B", 0, 0, 1, 0, "", {}, false, EventManager::DispatchMode::Immediate);

  BOOST_CHECK_EQUAL(firstNPC.get(), secondNPC.get()); // Same pooled event reused
}

BOOST_FIXTURE_TEST_CASE(EventPoolRecycling_CollisionEvents, EventManagerFixture) {
  EventPtr firstColl = nullptr;
  EventPtr secondColl = nullptr;
  EventManager::Instance().registerHandler(
      EventTypeId::Collision, [&](const EventData &data) {
        if (!firstColl) firstColl = data.event;
        else secondColl = data.event;
      });

  HammerEngine::CollisionInfo info{};
  EventManager::Instance().triggerCollision(info, EventManager::DispatchMode::Immediate);
  EventManager::Instance().triggerCollision(info, EventManager::DispatchMode::Immediate);

  BOOST_CHECK_EQUAL(firstColl.get(), secondColl.get()); // Same pooled event reused
}

BOOST_FIXTURE_TEST_CASE(EventPoolRecycling_ParticleEffectEvents, EventManagerFixture) {
  EventPtr firstParticle = nullptr;
  EventPtr secondParticle = nullptr;
  EventManager::Instance().registerHandler(
      EventTypeId::ParticleEffect, [&](const EventData &data) {
        if (!firstParticle) firstParticle = data.event;
        else secondParticle = data.event;
      });

  EventManager::Instance().triggerParticleEffect("Fire", 0, 0, 1.0f, 1.0f, "",
                                                  EventManager::DispatchMode::Immediate);
  EventManager::Instance().triggerParticleEffect("Fire", 0, 0, 1.0f, 1.0f, "",
                                                  EventManager::DispatchMode::Immediate);

  BOOST_CHECK_EQUAL(firstParticle.get(), secondParticle.get()); // Same pooled event reused
}

// ==================== Thread Safety Tests ====================

BOOST_FIXTURE_TEST_CASE(ThreadSafety_ConcurrentTriggers, EventManagerFixture) {
  EventManager::Instance().clean();
  BOOST_CHECK(EventManager::Instance().init());

  std::atomic<int> handlerCallCount{0};

  EventManager::Instance().registerHandler(
      EventTypeId::ResourceChange,
      [&handlerCallCount](const EventData &) { handlerCallCount.fetch_add(1); });

  HammerEngine::ResourceHandle testResource(1, 1);

  // Trigger multiple resource change events concurrently with immediate dispatch
  std::vector<std::thread> threads;
  for (int i = 0; i < 5; ++i) {
    threads.emplace_back([i, &testResource]() {
      EventManager::Instance().triggerResourceChange(
          TEST_PLAYER_HANDLE, testResource, i * 5, (i + 1) * 5, "concurrent_test",
          EventManager::DispatchMode::Immediate);
    });
  }

  // Wait for all threads
  for (auto &thread : threads) {
    thread.join();
  }

  // Allow a bit of time for any async operations to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Verify all events were processed
  BOOST_CHECK_GE(handlerCallCount.load(), 5);
}

// ==================== State Transition Tests ====================

BOOST_FIXTURE_TEST_CASE(StateTransitionPreparation_CleansUpProperly, EventManagerFixture) {
  EventManager::Instance().clean();
  BOOST_CHECK(EventManager::Instance().init());

  // Register a handler
  bool handlerCalled = false;
  EventManager::Instance().registerHandler(
      EventTypeId::Custom,
      [&handlerCalled](const EventData &) { handlerCalled = true; });

  // Test state transition preparation
  EventManager::Instance().prepareForStateTransition();

  // Verify manager is still functional after preparation
  BOOST_CHECK(EventManager::Instance().isInitialized());

  // Handlers should be cleared
  auto mockEvent = std::make_shared<MockEvent>("Test");
  EventManager::Instance().dispatchEvent(mockEvent, EventManager::DispatchMode::Immediate);
  BOOST_CHECK(!handlerCalled); // Handler was removed during transition prep
}

// ==================== Threading Control Tests (Debug Only) ====================

#ifndef NDEBUG
BOOST_FIXTURE_TEST_CASE(DynamicThreadingControl, EventManagerFixture) {
  EventManager::Instance().clean();
  BOOST_CHECK(EventManager::Instance().init());

  std::atomic<int> handlerCallCount{0};

  EventManager::Instance().registerHandler(
      EventTypeId::Weather,
      [&handlerCallCount](const EventData &) { handlerCallCount.fetch_add(1); });

  // Test with threading disabled
  EventManager::Instance().enableThreading(false);

  // Trigger event with deferred dispatch
  BOOST_CHECK(EventManager::Instance().changeWeather("Clear", 1.0f));
  EventManager::Instance().update();

  // Allow processing time
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  int callsWithoutThreading = handlerCallCount.load();
  BOOST_CHECK_GE(callsWithoutThreading, 1);

  // Reset counter and enable threading
  handlerCallCount.store(0);
  EventManager::Instance().enableThreading(true);

  // Trigger another event
  BOOST_CHECK(EventManager::Instance().changeWeather("Rainy", 1.0f));
  EventManager::Instance().update();

  // Allow threaded processing time
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  int callsWithThreading = handlerCallCount.load();
  BOOST_CHECK_GE(callsWithThreading, 1);

  EventManager::Instance().enableThreading(false);
}
#endif // NDEBUG
