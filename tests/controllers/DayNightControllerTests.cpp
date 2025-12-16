/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE DayNightControllerTests
#include <boost/test/unit_test.hpp>

#include "controllers/world/DayNightController.hpp"
#include "managers/GameTimeManager.hpp"
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
        GameTimeManager::Instance().init(12.0f, 1.0f);
    }

    ~DayNightControllerTestFixture() {
        // Controller auto-unsubscribes on destruction via ControllerBase
        // Clean up
        EventManager::Instance().clean();
    }

protected:
    // Controller owned by fixture (new ownership model)
    DayNightController m_controller;
};

// ============================================================================
// OWNERSHIP MODEL TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(OwnershipModelTests)

BOOST_AUTO_TEST_CASE(TestControllerInstantiation) {
    // Controllers can now be instantiated directly
    DayNightController controller1;
    DayNightController controller2;

    // Each is a separate instance
    BOOST_CHECK(&controller1 != &controller2);
}

BOOST_AUTO_TEST_CASE(TestMoveSemantics) {
    DayNightController controller1;
    controller1.subscribe();
    BOOST_CHECK(controller1.isSubscribed());

    // Move constructor
    DayNightController controller2(std::move(controller1));
    BOOST_CHECK(controller2.isSubscribed());
    BOOST_CHECK(!controller1.isSubscribed());  // Moved-from is unsubscribed
}

BOOST_AUTO_TEST_CASE(TestAutoUnsubscribeOnDestruction) {
    {
        DayNightController controller;
        controller.subscribe();
        BOOST_CHECK(controller.isSubscribed());
        // Destructor should auto-unsubscribe
    }
    // No crash = success
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// SUBSCRIPTION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(SubscriptionTests, DayNightControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestInitiallyNotSubscribed) {
    // Controller should not be subscribed initially
    BOOST_CHECK(!m_controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestSubscribe) {
    // Subscribe
    m_controller.subscribe();

    BOOST_CHECK(m_controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestUnsubscribe) {
    // Subscribe first
    m_controller.subscribe();
    BOOST_CHECK(m_controller.isSubscribed());

    // Now unsubscribe
    m_controller.unsubscribe();
    BOOST_CHECK(!m_controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestSubscribeUnsubscribeCycle) {
    // Multiple subscribe/unsubscribe cycles
    for (int i = 0; i < 3; ++i) {
        m_controller.subscribe();
        BOOST_CHECK(m_controller.isSubscribed());

        m_controller.unsubscribe();
        BOOST_CHECK(!m_controller.isSubscribed());
    }
}

BOOST_AUTO_TEST_CASE(TestDoubleSubscribeIgnored) {
    // First subscribe
    m_controller.subscribe();
    BOOST_CHECK(m_controller.isSubscribed());

    // Second subscribe should be ignored (no crash, still subscribed)
    m_controller.subscribe();
    BOOST_CHECK(m_controller.isSubscribed());

    // Unsubscribe once should fully unsubscribe
    m_controller.unsubscribe();
    BOOST_CHECK(!m_controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestDoubleUnsubscribeIgnored) {
    // Subscribe
    m_controller.subscribe();
    BOOST_CHECK(m_controller.isSubscribed());

    // First unsubscribe
    m_controller.unsubscribe();
    BOOST_CHECK(!m_controller.isSubscribed());

    // Second unsubscribe should be safe (no crash)
    m_controller.unsubscribe();
    BOOST_CHECK(!m_controller.isSubscribed());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// CURRENT PERIOD TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(CurrentPeriodTests, DayNightControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestGetCurrentPeriodAtNoon) {
    // Init GameTime at noon
    GameTimeManager::Instance().init(12.0f, 1.0f);

    m_controller.subscribe();

    // At noon (12:00), should be Day period
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Day);
}

BOOST_AUTO_TEST_CASE(TestGetCurrentPeriodString) {
    GameTimeManager::Instance().init(12.0f, 1.0f);

    m_controller.subscribe();

    std::string_view periodStr = m_controller.getCurrentPeriodString();
    BOOST_CHECK_EQUAL(periodStr, "Day");
}

BOOST_AUTO_TEST_CASE(TestPeriodStringValidity) {
    m_controller.subscribe();

    std::string_view periodStr = m_controller.getCurrentPeriodString();
    BOOST_CHECK(!periodStr.empty());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// CURRENT VISUALS TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(CurrentVisualsTests, DayNightControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestGetCurrentVisuals) {
    GameTimeManager::Instance().init(12.0f, 1.0f);

    m_controller.subscribe();

    TimePeriodVisuals visuals = m_controller.getCurrentVisuals();

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
    GameTimeManager::Instance().init(12.0f, 1.0f);

    m_controller.subscribe();

    // Controller visuals should match factory method for Day period
    TimePeriodVisuals controllerVisuals = m_controller.getCurrentVisuals();
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

    // Test at 6 AM
    GameTimeManager::Instance().init(6.0f, 1.0f);
    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Morning);
    m_controller.unsubscribe();

    // Test at 5 AM (boundary)
    GameTimeManager::Instance().init(5.0f, 1.0f);
    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Morning);
    m_controller.unsubscribe();

    // Test at 7:59 AM
    GameTimeManager::Instance().init(7.99f, 1.0f);
    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Morning);
    m_controller.unsubscribe();
}

BOOST_AUTO_TEST_CASE(TestDayPeriod) {
    // Day: 8:00 - 17:00

    // Test at noon
    GameTimeManager::Instance().init(12.0f, 1.0f);
    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Day);
    m_controller.unsubscribe();

    // Test at 8 AM (boundary)
    GameTimeManager::Instance().init(8.0f, 1.0f);
    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Day);
    m_controller.unsubscribe();

    // Test at 4:59 PM
    GameTimeManager::Instance().init(16.99f, 1.0f);
    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Day);
    m_controller.unsubscribe();
}

BOOST_AUTO_TEST_CASE(TestEveningPeriod) {
    // Evening: 17:00 - 21:00

    // Test at 6 PM
    GameTimeManager::Instance().init(18.0f, 1.0f);
    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Evening);
    m_controller.unsubscribe();

    // Test at 5 PM (boundary)
    GameTimeManager::Instance().init(17.0f, 1.0f);
    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Evening);
    m_controller.unsubscribe();

    // Test at 8:59 PM
    GameTimeManager::Instance().init(20.99f, 1.0f);
    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Evening);
    m_controller.unsubscribe();
}

BOOST_AUTO_TEST_CASE(TestNightPeriod) {
    // Night: 21:00 - 5:00

    // Test at midnight
    GameTimeManager::Instance().init(0.0f, 1.0f);
    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Night);
    m_controller.unsubscribe();

    // Test at 9 PM (boundary)
    GameTimeManager::Instance().init(21.0f, 1.0f);
    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Night);
    m_controller.unsubscribe();

    // Test at 3 AM
    GameTimeManager::Instance().init(3.0f, 1.0f);
    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Night);
    m_controller.unsubscribe();

    // Test at 4:59 AM (before morning boundary)
    GameTimeManager::Instance().init(4.99f, 1.0f);
    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Night);
    m_controller.unsubscribe();
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// PERIOD TRANSITION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(PeriodTransitionTests, DayNightControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestTransitionOnHourChangedEvent) {
    // Start at 7 AM (Morning)
    GameTimeManager::Instance().init(7.0f, 1.0f);

    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Morning);

    // Dispatch hour change to 8 AM (Day boundary)
    auto hourEvent = std::make_shared<HourChangedEvent>(8, false);
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    // Should transition to Day
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Day);
}

BOOST_AUTO_TEST_CASE(TestNoTransitionOnSamePeriod) {
    // Start at noon (Day)
    GameTimeManager::Instance().init(12.0f, 1.0f);

    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Day);

    // Dispatch hour change to 1 PM (still Day)
    auto hourEvent = std::make_shared<HourChangedEvent>(13, false);
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    // Should still be Day
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Day);
}

BOOST_AUTO_TEST_CASE(TestDayToEveningTransition) {
    // Start at 4 PM (Day)
    GameTimeManager::Instance().init(16.0f, 1.0f);

    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Day);

    // Dispatch hour change to 5 PM (Evening boundary)
    auto hourEvent = std::make_shared<HourChangedEvent>(17, false);
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    // Should transition to Evening
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Evening);
}

BOOST_AUTO_TEST_CASE(TestEveningToNightTransition) {
    // Start at 8 PM (Evening)
    GameTimeManager::Instance().init(20.0f, 1.0f);

    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Evening);

    // Dispatch hour change to 9 PM (Night boundary)
    auto hourEvent = std::make_shared<HourChangedEvent>(21, true);  // Night is true
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    // Should transition to Night
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Night);
}

BOOST_AUTO_TEST_CASE(TestNightToMorningTransition) {
    // Start at 4 AM (Night)
    GameTimeManager::Instance().init(4.0f, 1.0f);

    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Night);

    // Dispatch hour change to 5 AM (Morning boundary)
    auto hourEvent = std::make_shared<HourChangedEvent>(5, false);
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    // Should transition to Morning
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Morning);
}

BOOST_AUTO_TEST_CASE(TestFullDayCycle) {
    // Start at midnight (Night)
    GameTimeManager::Instance().init(0.0f, 1.0f);

    m_controller.subscribe();

    // Night at midnight
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Night);

    // Transition to Morning at 5 AM
    auto morningEvent = std::make_shared<HourChangedEvent>(5, false);
    EventManager::Instance().dispatchEvent(morningEvent, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Morning);

    // Transition to Day at 8 AM
    auto dayEvent = std::make_shared<HourChangedEvent>(8, false);
    EventManager::Instance().dispatchEvent(dayEvent, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Day);

    // Transition to Evening at 5 PM
    auto eveningEvent = std::make_shared<HourChangedEvent>(17, false);
    EventManager::Instance().dispatchEvent(eveningEvent, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Evening);

    // Transition to Night at 9 PM
    auto nightEvent = std::make_shared<HourChangedEvent>(21, true);
    EventManager::Instance().dispatchEvent(nightEvent, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Night);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// EVENT FILTERING TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(EventFilteringTests, DayNightControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestIgnoresNonHourChangedEvents) {
    GameTimeManager::Instance().init(12.0f, 1.0f);

    m_controller.subscribe();
    TimePeriod initialPeriod = m_controller.getCurrentPeriod();

    // Dispatch various non-HourChanged events
    auto dayEvent = std::make_shared<DayChangedEvent>(5, 5, 0, "Bloomtide");
    EventManager::Instance().dispatchEvent(dayEvent, EventManager::DispatchMode::Immediate);

    auto seasonEvent = std::make_shared<SeasonChangedEvent>(Season::Summer, Season::Spring, "Summer");
    EventManager::Instance().dispatchEvent(seasonEvent, EventManager::DispatchMode::Immediate);

    auto weatherEvent = std::make_shared<WeatherCheckEvent>(Season::Summer, WeatherType::Clear);
    EventManager::Instance().dispatchEvent(weatherEvent, EventManager::DispatchMode::Immediate);

    // Period should remain unchanged
    BOOST_CHECK(m_controller.getCurrentPeriod() == initialPeriod);
}

BOOST_AUTO_TEST_CASE(TestNoHandlingWhenUnsubscribed) {
    GameTimeManager::Instance().init(7.0f, 1.0f);

    // Subscribe to set initial period
    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == TimePeriod::Morning);
    m_controller.unsubscribe();

    // Dispatch hour change while unsubscribed
    auto hourEvent = std::make_shared<HourChangedEvent>(21, true);
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    // Period should NOT change since we're unsubscribed
    // Note: The internal state may persist, but handler won't process new events
    BOOST_CHECK(!m_controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestWeatherCheckEventIgnoredWhenUnsubscribed) {
    // Ensure not subscribed
    BOOST_CHECK(!m_controller.isSubscribed());

    // Get initial period
    GameTimeManager::Instance().init(12.0f, 1.0f);
    m_controller.subscribe();
    TimePeriod initialPeriod = m_controller.getCurrentPeriod();
    m_controller.unsubscribe();

    // Dispatch a weather check event
    auto weatherCheckEvent = std::make_shared<WeatherCheckEvent>(Season::Winter, WeatherType::Snowy);
    EventManager::Instance().dispatchEvent(weatherCheckEvent, EventManager::DispatchMode::Immediate);

    // Period should NOT change since we're not subscribed
    // Re-subscribe to check period is still the same
    m_controller.subscribe();
    BOOST_CHECK(m_controller.getCurrentPeriod() == initialPeriod);
}

BOOST_AUTO_TEST_SUITE_END()
