/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE EventManagerTest
// BOOST_TEST_NO_SIGNAL_HANDLING is already defined on the command line
#include <boost/test/included/unit_test.hpp>

#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

#include "../../include/core/ThreadSystem.hpp"
#include "../../include/managers/EventManager.hpp"
#include "../../include/events/Event.hpp"
#include "../../include/events/WeatherEvent.hpp"
#include "../../include/events/SceneChangeEvent.hpp"
#include "../../include/events/NPCSpawnEvent.hpp"


// Mock Event class for testing
class MockEvent : public Event {
public:
    MockEvent(const std::string& name) : m_name(name), m_executed(false), m_conditionsMet(false) {}

    void update() override {
        m_updated = true;
    }

    void execute() override {
        m_executed = true;
    }

    void reset() override {
        m_updated = false;
        m_executed = false;
        m_conditionsMet = false;
    }

    void clean() override {}

    std::string getName() const override { return m_name; }
    std::string getType() const override { return "Mock"; }

    bool checkConditions() override {
        return m_conditionsMet;
    }

    void setConditionsMet(bool met) { m_conditionsMet = met; }
    bool wasExecuted() const { return m_executed; }
    bool wasUpdated() const { return m_updated; }

private:
    std::string m_name;
    bool m_executed{false};
    bool m_updated{false};
    bool m_conditionsMet{false};
};

struct EventManagerFixture {
    EventManagerFixture() {
        // Ensure ThreadSystem is initialized first if it exists
        if (Forge::ThreadSystem::Exists()) {
            Forge::ThreadSystem::Instance().init();
        }

        EventManager::Instance().clean();
        BOOST_CHECK(EventManager::Instance().init());
    }

    ~EventManagerFixture() {
        // Ensure threading is disabled before cleanup to avoid potential issues
        EventManager::Instance().configureThreading(false, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        
        // Clean up the EventManager
        EventManager::Instance().clean();

        // Note: We don't clean up ThreadSystem here since
        // other tests might use it and it's a singleton
    }
};

// Test basic initialization and cleanup
BOOST_FIXTURE_TEST_CASE(InitAndClean, EventManagerFixture) {
    BOOST_CHECK(EventManager::Instance().init());
    BOOST_CHECK_EQUAL(EventManager::Instance().getEventCount(), 0);
    EventManager::Instance().clean();
}

// Test event registration and retrieval
BOOST_FIXTURE_TEST_CASE(RegisterAndRetrieveEvent, EventManagerFixture) {
    auto mockEvent = std::make_shared<MockEvent>("TestEvent");
    EventManager::Instance().registerEvent("TestEvent", mockEvent);

    BOOST_CHECK(EventManager::Instance().hasEvent("TestEvent"));
    BOOST_CHECK_EQUAL(EventManager::Instance().getEventCount(), 1);

    Event* retrievedEvent = EventManager::Instance().getEvent("TestEvent");
    BOOST_REQUIRE(retrievedEvent != nullptr);
    BOOST_CHECK_EQUAL(retrievedEvent->getName(), "TestEvent");
    BOOST_CHECK_EQUAL(retrievedEvent->getType(), "Mock");
}

// Test event activation/deactivation
BOOST_FIXTURE_TEST_CASE(EventActivation, EventManagerFixture) {
    auto mockEvent = std::make_shared<MockEvent>("TestEvent");
    EventManager::Instance().registerEvent("TestEvent", mockEvent);

    // Events should be active by default
    BOOST_CHECK(EventManager::Instance().isEventActive("TestEvent"));

    // Test deactivation
    EventManager::Instance().setEventActive("TestEvent", false);
    BOOST_CHECK(!EventManager::Instance().isEventActive("TestEvent"));

    // Test reactivation
    EventManager::Instance().setEventActive("TestEvent", true);
    BOOST_CHECK(EventManager::Instance().isEventActive("TestEvent"));
}

// Test event execution
BOOST_FIXTURE_TEST_CASE(EventExecution, EventManagerFixture) {
    auto mockEvent = std::make_shared<MockEvent>("TestEvent");
    EventManager::Instance().registerEvent("TestEvent", mockEvent);

    // Execute the event
    BOOST_CHECK(EventManager::Instance().executeEvent("TestEvent"));
    BOOST_CHECK(dynamic_cast<MockEvent*>(EventManager::Instance().getEvent("TestEvent"))->wasExecuted());
}

// Test event update and condition-based execution
BOOST_FIXTURE_TEST_CASE(EventUpdateAndConditions, EventManagerFixture) {
    // Start with a completely clean EventManager
    EventManager::Instance().clean();
    BOOST_CHECK(EventManager::Instance().init());

    // CRITICAL: Disable threading explicitly and wait for it to take effect
    EventManager::Instance().configureThreading(false, 0);
    // Allow time for ThreadSystem tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Create a simple one-time event with no conditions initially
    auto mockEvent = std::make_shared<MockEvent>("SimpleEvent");
    BOOST_CHECK(mockEvent != nullptr);

    // Configure event as one-time and inactive initially
    mockEvent->setOneTime(true);
    mockEvent->setActive(false);
    mockEvent->setConditionsMet(false);

    // Register it and verify registration
    EventManager::Instance().registerEvent("SimpleEvent", mockEvent);
    BOOST_CHECK(EventManager::Instance().hasEvent("SimpleEvent"));
    BOOST_CHECK_EQUAL(EventManager::Instance().getEventCount(), 1);

    // Now activate the event
    EventManager::Instance().setEventActive("SimpleEvent", true);

    // Keep a direct pointer to the event for checking state
    MockEvent* eventPtr = mockEvent.get();
    BOOST_REQUIRE(eventPtr != nullptr);

    // TEST PHASE 1: Event with false conditions shouldn't execute
    EventManager::Instance().update();
    // Wait for any ThreadSystem tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    BOOST_CHECK(eventPtr->wasUpdated());
    BOOST_CHECK(!eventPtr->wasExecuted());

    // Reset event for next test
    eventPtr->reset();

    // TEST PHASE 2: Event with true conditions should execute
    eventPtr->setConditionsMet(true);
    EventManager::Instance().update();
    // Wait for any ThreadSystem tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    BOOST_CHECK(eventPtr->wasUpdated());
    BOOST_CHECK(eventPtr->wasExecuted());

    // Clean up immediately
    EventManager::Instance().removeEvent("SimpleEvent");
    EventManager::Instance().clean();

    // Test passes if we got here
    BOOST_CHECK(true);
}

// Test event removal
BOOST_FIXTURE_TEST_CASE(EventRemoval, EventManagerFixture) {
    auto mockEvent = std::make_shared<MockEvent>("TestEvent");
    EventManager::Instance().registerEvent("TestEvent", mockEvent);

    BOOST_CHECK(EventManager::Instance().hasEvent("TestEvent"));
    BOOST_CHECK(EventManager::Instance().removeEvent("TestEvent"));
    BOOST_CHECK(!EventManager::Instance().hasEvent("TestEvent"));
    BOOST_CHECK_EQUAL(EventManager::Instance().getEventCount(), 0);
}

// Test event type retrieval
BOOST_FIXTURE_TEST_CASE(EventTypeRetrieval, EventManagerFixture) {
    auto mockEvent1 = std::make_shared<MockEvent>("TestEvent1");
    auto mockEvent2 = std::make_shared<MockEvent>("TestEvent2");
    auto rainEvent = std::make_shared<WeatherEvent>("RainEvent", WeatherType::Rainy);

    EventManager::Instance().registerEvent("TestEvent1", mockEvent1);
    EventManager::Instance().registerEvent("TestEvent2", mockEvent2);
    EventManager::Instance().registerEvent("RainEvent", std::static_pointer_cast<Event>(rainEvent));

    auto mockEvents = EventManager::Instance().getEventsByType("Mock");
    BOOST_CHECK_EQUAL(mockEvents.size(), 2);

    auto weatherEvents = EventManager::Instance().getEventsByType("Weather");
    BOOST_CHECK_EQUAL(weatherEvents.size(), 1);
}

// Test messaging system
BOOST_FIXTURE_TEST_CASE(EventMessaging, EventManagerFixture) {
    class MessageTestEvent : public MockEvent {
    public:
        MessageTestEvent(const std::string& name) : MockEvent(name), m_lastMessage("") {}

        void onMessage(const std::string& message) override {
            m_lastMessage = message;
            m_messageReceived = true;
        }

        std::string getLastMessage() const { return m_lastMessage; }
        bool wasMessageReceived() const { return m_messageReceived; }

    private:
        std::string m_lastMessage;
        bool m_messageReceived{false};
    };

    auto event1 = std::make_shared<MessageTestEvent>("Event1");
    auto event2 = std::make_shared<MessageTestEvent>("Event2");

    EventManager::Instance().registerEvent("Event1", event1);
    EventManager::Instance().registerEvent("Event2", event2);

    // Test direct message
    EventManager::Instance().sendMessageToEvent("Event1", "TestMessage", true);
    BOOST_CHECK(dynamic_cast<MessageTestEvent*>(EventManager::Instance().getEvent("Event1"))->wasMessageReceived());
    BOOST_CHECK_EQUAL(dynamic_cast<MessageTestEvent*>(EventManager::Instance().getEvent("Event1"))->getLastMessage(), "TestMessage");
    BOOST_CHECK(!dynamic_cast<MessageTestEvent*>(EventManager::Instance().getEvent("Event2"))->wasMessageReceived());

    // Test broadcast
    EventManager::Instance().broadcastMessage("BroadcastMessage", true);
    BOOST_CHECK_EQUAL(dynamic_cast<MessageTestEvent*>(EventManager::Instance().getEvent("Event1"))->getLastMessage(), "BroadcastMessage");
    BOOST_CHECK_EQUAL(dynamic_cast<MessageTestEvent*>(EventManager::Instance().getEvent("Event2"))->getLastMessage(), "BroadcastMessage");
}

// Test weather events
BOOST_FIXTURE_TEST_CASE(WeatherEvents, EventManagerFixture) {
    auto rainEvent = std::make_shared<WeatherEvent>("Rain", WeatherType::Rainy);
    EventManager::Instance().registerEvent("Rain", std::static_pointer_cast<Event>(rainEvent));

    // Test direct weather change
    BOOST_CHECK(EventManager::Instance().changeWeather("Rainy", 2.0f));

    // Test weather event execution
    BOOST_CHECK(EventManager::Instance().executeEvent("Rain"));
}

// Test scene change events
BOOST_FIXTURE_TEST_CASE(SceneChangeEvents, EventManagerFixture) {
    auto sceneEvent = std::make_shared<SceneChangeEvent>("ToMainMenu", "MainMenu");
    EventManager::Instance().registerEvent("ToMainMenu", std::static_pointer_cast<Event>(sceneEvent));

    // Test direct scene change
    BOOST_CHECK(EventManager::Instance().changeScene("MainMenu", "fade", 1.0f));

    // Test scene event execution
    BOOST_CHECK(EventManager::Instance().executeEvent("ToMainMenu"));
}

// Test NPC spawn events
BOOST_FIXTURE_TEST_CASE(NPCSpawnEvents, EventManagerFixture) {
    auto spawnEvent = std::make_shared<NPCSpawnEvent>("SpawnGuard", "Guard");
    EventManager::Instance().registerEvent("SpawnGuard", std::static_pointer_cast<Event>(spawnEvent));

    // Test direct NPC spawn
    BOOST_CHECK(EventManager::Instance().spawnNPC("Guard", 100.0f, 200.0f));

    // Test spawn event execution
    BOOST_CHECK(EventManager::Instance().executeEvent("SpawnGuard"));
}

// Test thread safety with minimal concurrent operations
BOOST_FIXTURE_TEST_CASE(ThreadSafety, EventManagerFixture) {
    // Start with clean state
    EventManager::Instance().clean();
    BOOST_CHECK(EventManager::Instance().init());

    // Test enabling threading with ThreadSystem
    EventManager::Instance().configureThreading(true, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Register a test event
    auto mockEvent = std::make_shared<MockEvent>("ThreadTest");
    EventManager::Instance().registerEvent("ThreadTest", mockEvent);

    // Set conditions and verify behavior
    dynamic_cast<MockEvent*>(EventManager::Instance().getEvent("ThreadTest"))->setConditionsMet(true);

    // Update with threading enabled
    EventManager::Instance().update();
    // Allow time for ThreadSystem tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify update worked
    BOOST_CHECK(dynamic_cast<MockEvent*>(EventManager::Instance().getEvent("ThreadTest"))->wasUpdated());
    BOOST_CHECK(dynamic_cast<MockEvent*>(EventManager::Instance().getEvent("ThreadTest"))->wasExecuted());

    // Test disabling threading
    EventManager::Instance().configureThreading(false, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Reset event and test again without threading
    dynamic_cast<MockEvent*>(EventManager::Instance().getEvent("ThreadTest"))->reset();
    dynamic_cast<MockEvent*>(EventManager::Instance().getEvent("ThreadTest"))->setConditionsMet(true);

    EventManager::Instance().update();

    // Verify update worked without threading
    BOOST_CHECK(dynamic_cast<MockEvent*>(EventManager::Instance().getEvent("ThreadTest"))->wasUpdated());
    BOOST_CHECK(dynamic_cast<MockEvent*>(EventManager::Instance().getEvent("ThreadTest"))->wasExecuted());

    // Make sure threading is disabled before cleanup
    EventManager::Instance().configureThreading(false, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Clean up
    EventManager::Instance().removeEvent("ThreadTest");

    // Test passes if we reached this point
    BOOST_CHECK(true);
}
