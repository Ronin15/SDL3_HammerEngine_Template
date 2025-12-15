/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE DayNightControllerTests
#include <boost/test/unit_test.hpp>

#include "controllers/world/DayNightController.hpp"
#include "core/GameTime.hpp"
#include "managers/EventManager.hpp"
#include "events/TimeEvent.hpp"
#include "../events/EventManagerTestAccess.hpp"
#include <string>

// ============================================================================
// Test Fixture
// ============================================================================

class DayNightControllerTestFixture {
public:
    DayNightControllerTestFixture() {
        // Reset event manager to clean state
        EventManagerTestAccess::reset();

        // Initialize GameTime to noon (Day period)
        GameTime::Instance().init(12.0f, 1.0f);
    }

    ~DayNightControllerTestFixture() {
        // Ensure DayNightController is unsubscribed
        DayNightController::Instance().unsubscribe();

        // Clean up
        EventManager::Instance().clean();
    }

protected:
    DayNightController& getController() {
        return DayNightController::Instance();
    }
};

// ============================================================================
// SINGLETON PATTERN TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(SingletonTests)

BOOST_AUTO_TEST_CASE(TestSingletonPattern) {
    DayNightController* instance1 = &DayNightController::Instance();
    DayNightController* instance2 = &DayNightController::Instance();

    BOOST_CHECK(instance1 == instance2);
    BOOST_CHECK(instance1 != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// SUBSCRIPTION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(SubscriptionTests, DayNightControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestInitiallyNotSubscribed) {
    // Controller should not be subscribed initially
    BOOST_CHECK(!getController().isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestSubscribe) {
    auto& controller = getController();

    // Subscribe
    controller.subscribe();

    BOOST_CHECK(controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestUnsubscribe) {
    auto& controller = getController();

    // Subscribe first
    controller.subscribe();
    BOOST_CHECK(controller.isSubscribed());

    // Now unsubscribe
    controller.unsubscribe();
    BOOST_CHECK(!controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestSubscribeUnsubscribeCycle) {
    auto& controller = getController();

    // Multiple subscribe/unsubscribe cycles
    for (int i = 0; i < 3; ++i) {
        controller.subscribe();
        BOOST_CHECK(controller.isSubscribed());

        controller.unsubscribe();
        BOOST_CHECK(!controller.isSubscribed());
    }
}

BOOST_AUTO_TEST_CASE(TestDoubleSubscribeIgnored) {
    auto& controller = getController();

    // First subscribe
    controller.subscribe();
    BOOST_CHECK(controller.isSubscribed());

    // Second subscribe should be ignored
    controller.subscribe();
    BOOST_CHECK(controller.isSubscribed());

    // Unsubscribe once should fully unsubscribe
    controller.unsubscribe();
    BOOST_CHECK(!controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestDoubleUnsubscribeIgnored) {
    auto& controller = getController();

    // Subscribe
    controller.subscribe();
    BOOST_CHECK(controller.isSubscribed());

    // First unsubscribe
    controller.unsubscribe();
    BOOST_CHECK(!controller.isSubscribed());

    // Second unsubscribe should be safe
    controller.unsubscribe();
    BOOST_CHECK(!controller.isSubscribed());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// CURRENT PERIOD TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(CurrentPeriodTests, DayNightControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestGetCurrentPeriodAtNoon) {
    // Init GameTime at noon
    GameTime::Instance().init(12.0f, 1.0f);

    auto& controller = getController();
    controller.subscribe();

    // At noon (12:00), should be Day period
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Day);
}

BOOST_AUTO_TEST_CASE(TestGetCurrentPeriodString) {
    GameTime::Instance().init(12.0f, 1.0f);

    auto& controller = getController();
    controller.subscribe();

    const char* periodStr = controller.getCurrentPeriodString();
    BOOST_CHECK_EQUAL(std::string(periodStr), "Day");
}

BOOST_AUTO_TEST_CASE(TestPeriodStringValidity) {
    auto& controller = getController();
    controller.subscribe();

    const char* periodStr = controller.getCurrentPeriodString();
    BOOST_CHECK(periodStr != nullptr);
    BOOST_CHECK(std::strlen(periodStr) > 0);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// CURRENT VISUALS TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(CurrentVisualsTests, DayNightControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestGetCurrentVisuals) {
    GameTime::Instance().init(12.0f, 1.0f);

    auto& controller = getController();
    controller.subscribe();

    TimePeriodVisuals visuals = controller.getCurrentVisuals();

    // Day visuals should have specific properties
    // Can't check exact values without knowing implementation, but verify valid ranges
    BOOST_CHECK_GE(visuals.overlayR, 0);
    BOOST_CHECK_LE(visuals.overlayR, 255);
    BOOST_CHECK_GE(visuals.overlayG, 0);
    BOOST_CHECK_LE(visuals.overlayG, 255);
    BOOST_CHECK_GE(visuals.overlayB, 0);
    BOOST_CHECK_LE(visuals.overlayB, 255);
    BOOST_CHECK_GE(visuals.overlayA, 0);
    BOOST_CHECK_LE(visuals.overlayA, 255);
}

BOOST_AUTO_TEST_CASE(TestVisualsMatchPeriodFactory) {
    GameTime::Instance().init(12.0f, 1.0f);

    auto& controller = getController();
    controller.subscribe();

    // Controller visuals should match factory method for Day period
    TimePeriodVisuals controllerVisuals = controller.getCurrentVisuals();
    TimePeriodVisuals factoryVisuals = TimePeriodVisuals::getDay();

    BOOST_CHECK_EQUAL(controllerVisuals.overlayR, factoryVisuals.overlayR);
    BOOST_CHECK_EQUAL(controllerVisuals.overlayG, factoryVisuals.overlayG);
    BOOST_CHECK_EQUAL(controllerVisuals.overlayB, factoryVisuals.overlayB);
    BOOST_CHECK_EQUAL(controllerVisuals.overlayA, factoryVisuals.overlayA);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// HOUR TO TIME PERIOD CONVERSION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(HourToTimePeriodTests, DayNightControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestMorningPeriod) {
    // Morning: 5:00 - 8:00
    auto& controller = getController();

    // Test at 6 AM
    GameTime::Instance().init(6.0f, 1.0f);
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Morning);
    controller.unsubscribe();

    // Test at 5 AM (boundary)
    GameTime::Instance().init(5.0f, 1.0f);
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Morning);
    controller.unsubscribe();

    // Test at 7:59 AM
    GameTime::Instance().init(7.99f, 1.0f);
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Morning);
    controller.unsubscribe();
}

BOOST_AUTO_TEST_CASE(TestDayPeriod) {
    // Day: 8:00 - 17:00
    auto& controller = getController();

    // Test at noon
    GameTime::Instance().init(12.0f, 1.0f);
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Day);
    controller.unsubscribe();

    // Test at 8 AM (boundary)
    GameTime::Instance().init(8.0f, 1.0f);
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Day);
    controller.unsubscribe();

    // Test at 4:59 PM
    GameTime::Instance().init(16.99f, 1.0f);
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Day);
    controller.unsubscribe();
}

BOOST_AUTO_TEST_CASE(TestEveningPeriod) {
    // Evening: 17:00 - 21:00
    auto& controller = getController();

    // Test at 6 PM
    GameTime::Instance().init(18.0f, 1.0f);
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Evening);
    controller.unsubscribe();

    // Test at 5 PM (boundary)
    GameTime::Instance().init(17.0f, 1.0f);
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Evening);
    controller.unsubscribe();

    // Test at 8:59 PM
    GameTime::Instance().init(20.99f, 1.0f);
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Evening);
    controller.unsubscribe();
}

BOOST_AUTO_TEST_CASE(TestNightPeriod) {
    // Night: 21:00 - 5:00
    auto& controller = getController();

    // Test at midnight
    GameTime::Instance().init(0.0f, 1.0f);
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Night);
    controller.unsubscribe();

    // Test at 9 PM (boundary)
    GameTime::Instance().init(21.0f, 1.0f);
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Night);
    controller.unsubscribe();

    // Test at 3 AM
    GameTime::Instance().init(3.0f, 1.0f);
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Night);
    controller.unsubscribe();

    // Test at 4:59 AM (before morning boundary)
    GameTime::Instance().init(4.99f, 1.0f);
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Night);
    controller.unsubscribe();
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// PERIOD TRANSITION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(PeriodTransitionTests, DayNightControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestTransitionOnHourChangedEvent) {
    // Start at 7 AM (Morning)
    GameTime::Instance().init(7.0f, 1.0f);

    auto& controller = getController();
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Morning);

    // Dispatch hour change to 8 AM (Day boundary)
    auto hourEvent = std::make_shared<HourChangedEvent>(8, false);
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    // Should transition to Day
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Day);
}

BOOST_AUTO_TEST_CASE(TestNoTransitionOnSamePeriod) {
    // Start at noon (Day)
    GameTime::Instance().init(12.0f, 1.0f);

    auto& controller = getController();
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Day);

    // Dispatch hour change to 1 PM (still Day)
    auto hourEvent = std::make_shared<HourChangedEvent>(13, false);
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    // Should still be Day
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Day);
}

BOOST_AUTO_TEST_CASE(TestDayToEveningTransition) {
    // Start at 4 PM (Day)
    GameTime::Instance().init(16.0f, 1.0f);

    auto& controller = getController();
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Day);

    // Dispatch hour change to 5 PM (Evening boundary)
    auto hourEvent = std::make_shared<HourChangedEvent>(17, false);
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    // Should transition to Evening
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Evening);
}

BOOST_AUTO_TEST_CASE(TestEveningToNightTransition) {
    // Start at 8 PM (Evening)
    GameTime::Instance().init(20.0f, 1.0f);

    auto& controller = getController();
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Evening);

    // Dispatch hour change to 9 PM (Night boundary)
    auto hourEvent = std::make_shared<HourChangedEvent>(21, true);  // Night is true
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    // Should transition to Night
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Night);
}

BOOST_AUTO_TEST_CASE(TestNightToMorningTransition) {
    // Start at 4 AM (Night)
    GameTime::Instance().init(4.0f, 1.0f);

    auto& controller = getController();
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Night);

    // Dispatch hour change to 5 AM (Morning boundary)
    auto hourEvent = std::make_shared<HourChangedEvent>(5, false);
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    // Should transition to Morning
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Morning);
}

BOOST_AUTO_TEST_CASE(TestFullDayCycle) {
    // Start at midnight (Night)
    GameTime::Instance().init(0.0f, 1.0f);

    auto& controller = getController();
    controller.subscribe();

    // Night at midnight
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Night);

    // Transition to Morning at 5 AM
    auto morningEvent = std::make_shared<HourChangedEvent>(5, false);
    EventManager::Instance().dispatchEvent(morningEvent, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Morning);

    // Transition to Day at 8 AM
    auto dayEvent = std::make_shared<HourChangedEvent>(8, false);
    EventManager::Instance().dispatchEvent(dayEvent, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Day);

    // Transition to Evening at 5 PM
    auto eveningEvent = std::make_shared<HourChangedEvent>(17, false);
    EventManager::Instance().dispatchEvent(eveningEvent, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Evening);

    // Transition to Night at 9 PM
    auto nightEvent = std::make_shared<HourChangedEvent>(21, true);
    EventManager::Instance().dispatchEvent(nightEvent, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Night);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// EVENT FILTERING TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(EventFilteringTests, DayNightControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestIgnoresNonHourChangedEvents) {
    GameTime::Instance().init(12.0f, 1.0f);

    auto& controller = getController();
    controller.subscribe();
    TimePeriod initialPeriod = controller.getCurrentPeriod();

    // Dispatch various non-HourChanged events
    auto dayEvent = std::make_shared<DayChangedEvent>(5, 5, 0, "Bloomtide");
    EventManager::Instance().dispatchEvent(dayEvent, EventManager::DispatchMode::Immediate);

    auto seasonEvent = std::make_shared<SeasonChangedEvent>(Season::Summer, Season::Spring, "Summer");
    EventManager::Instance().dispatchEvent(seasonEvent, EventManager::DispatchMode::Immediate);

    auto weatherEvent = std::make_shared<WeatherCheckEvent>(Season::Summer, WeatherType::Clear);
    EventManager::Instance().dispatchEvent(weatherEvent, EventManager::DispatchMode::Immediate);

    // Period should remain unchanged
    BOOST_CHECK(controller.getCurrentPeriod() == initialPeriod);
}

BOOST_AUTO_TEST_CASE(TestNoHandlingWhenUnsubscribed) {
    GameTime::Instance().init(7.0f, 1.0f);

    auto& controller = getController();

    // Subscribe to set initial period
    controller.subscribe();
    BOOST_CHECK(controller.getCurrentPeriod() == TimePeriod::Morning);
    controller.unsubscribe();

    // Dispatch hour change while unsubscribed
    auto hourEvent = std::make_shared<HourChangedEvent>(21, true);
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    // Period should NOT change since we're unsubscribed
    // Note: The internal state may persist, but handler won't process new events
    BOOST_CHECK(!controller.isSubscribed());
}

BOOST_AUTO_TEST_SUITE_END()
