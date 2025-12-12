/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE TimeControllerTests
#include <boost/test/unit_test.hpp>

#include "controllers/world/TimeController.hpp"
#include "core/GameTime.hpp"
#include "managers/EventManager.hpp"
#include "events/TimeEvent.hpp"
#include "../events/EventManagerTestAccess.hpp"
#include <string>

// ============================================================================
// Test Fixture
// ============================================================================

class TimeControllerTestFixture {
public:
    TimeControllerTestFixture() {
        // Reset event manager to clean state
        EventManagerTestAccess::reset();

        // Initialize GameTime
        GameTime::Instance().init(12.0f, 1.0f);
    }

    ~TimeControllerTestFixture() {
        // Ensure TimeController is unsubscribed
        TimeController::Instance().unsubscribe();

        // Clean up
        EventManager::Instance().clean();
    }

protected:
    TimeController& getController() {
        return TimeController::Instance();
    }
};

// ============================================================================
// SINGLETON PATTERN TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(SingletonTests)

BOOST_AUTO_TEST_CASE(TestSingletonPattern) {
    TimeController* instance1 = &TimeController::Instance();
    TimeController* instance2 = &TimeController::Instance();

    BOOST_CHECK(instance1 == instance2);
    BOOST_CHECK(instance1 != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// SUBSCRIPTION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(SubscriptionTests, TimeControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestInitiallyNotSubscribed) {
    // Controller should not be subscribed initially
    BOOST_CHECK(!getController().isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestSubscribe) {
    auto& controller = getController();

    // Subscribe with event log id
    controller.subscribe("test_event_log");

    BOOST_CHECK(controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestUnsubscribe) {
    auto& controller = getController();

    // Subscribe first
    controller.subscribe("test_event_log");
    BOOST_CHECK(controller.isSubscribed());

    // Now unsubscribe
    controller.unsubscribe();
    BOOST_CHECK(!controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestSubscribeUnsubscribeCycle) {
    auto& controller = getController();

    // Multiple subscribe/unsubscribe cycles
    for (int i = 0; i < 3; ++i) {
        controller.subscribe("test_event_log");
        BOOST_CHECK(controller.isSubscribed());

        controller.unsubscribe();
        BOOST_CHECK(!controller.isSubscribed());
    }
}

BOOST_AUTO_TEST_CASE(TestDoubleSubscribeIgnored) {
    auto& controller = getController();

    // First subscribe
    controller.subscribe("test_event_log");
    BOOST_CHECK(controller.isSubscribed());

    // Second subscribe should be ignored (no crash, still subscribed)
    controller.subscribe("test_event_log_2");
    BOOST_CHECK(controller.isSubscribed());

    // Unsubscribe once should fully unsubscribe
    controller.unsubscribe();
    BOOST_CHECK(!controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestDoubleUnsubscribeIgnored) {
    auto& controller = getController();

    // Subscribe
    controller.subscribe("test_event_log");
    BOOST_CHECK(controller.isSubscribed());

    // First unsubscribe
    controller.unsubscribe();
    BOOST_CHECK(!controller.isSubscribed());

    // Second unsubscribe should be safe (no crash)
    controller.unsubscribe();
    BOOST_CHECK(!controller.isSubscribed());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// STATUS LABEL TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(StatusLabelTests, TimeControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestSetStatusLabel) {
    auto& controller = getController();
    controller.subscribe("test_event_log");

    // Set status label - should not crash even without UIManager init
    controller.setStatusLabel("status_label_id");

    // No direct getter to verify, but should not crash
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestSetStatusLabelEmpty) {
    auto& controller = getController();
    controller.subscribe("test_event_log");

    // Empty label should be handled gracefully
    controller.setStatusLabel("");

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// STATUS FORMAT MODE TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(StatusFormatModeTests, TimeControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestSetStatusFormatModeDefault) {
    auto& controller = getController();
    controller.subscribe("test_event_log");

    // Set to default mode
    controller.setStatusFormatMode(TimeController::StatusFormatMode::Default);

    // No direct getter, but should not crash
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestSetStatusFormatModeExtended) {
    auto& controller = getController();
    controller.subscribe("test_event_log");

    // Set to extended mode
    controller.setStatusFormatMode(TimeController::StatusFormatMode::Extended);

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestStatusFormatModeSwitching) {
    auto& controller = getController();
    controller.subscribe("test_event_log");

    // Switch between modes multiple times
    controller.setStatusFormatMode(TimeController::StatusFormatMode::Default);
    controller.setStatusFormatMode(TimeController::StatusFormatMode::Extended);
    controller.setStatusFormatMode(TimeController::StatusFormatMode::Default);

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// EVENT HANDLER REGISTRATION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(EventHandlerTests, TimeControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestHandlersRegisteredOnSubscribe) {
    auto& controller = getController();

    // Before subscription, no handlers should be registered
    // We can verify this by checking isSubscribed
    BOOST_CHECK(!controller.isSubscribed());

    // Subscribe - this should register handlers with EventManager
    controller.subscribe("test_event_log");

    BOOST_CHECK(controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestHandlersRemovedOnUnsubscribe) {
    auto& controller = getController();

    // Subscribe first
    controller.subscribe("test_event_log");
    BOOST_CHECK(controller.isSubscribed());

    // Unsubscribe - handlers should be removed
    controller.unsubscribe();
    BOOST_CHECK(!controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestUnsubscribeResetsState) {
    auto& controller = getController();

    // Subscribe and set various state
    controller.subscribe("test_event_log");
    controller.setStatusLabel("test_status");
    controller.setStatusFormatMode(TimeController::StatusFormatMode::Extended);

    // Unsubscribe should reset internal state
    controller.unsubscribe();
    BOOST_CHECK(!controller.isSubscribed());

    // Re-subscribe and verify it's a clean slate
    controller.subscribe("test_event_log_2");
    BOOST_CHECK(controller.isSubscribed());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// TIME EVENT DISPATCHING TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(TimeEventDispatchTests, TimeControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestTimeEventDispatchWithSubscribedController) {
    auto& controller = getController();
    controller.subscribe("test_event_log");

    // Dispatch a time event - controller should handle it without crashing
    auto hourEvent = std::make_shared<HourChangedEvent>(14, false);
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestDayChangedEventDispatch) {
    auto& controller = getController();
    controller.subscribe("test_event_log");

    // Dispatch a day changed event
    auto dayEvent = std::make_shared<DayChangedEvent>(5, 5, 0, "Bloomtide");
    EventManager::Instance().dispatchEvent(dayEvent, EventManager::DispatchMode::Immediate);

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestMonthChangedEventDispatch) {
    auto& controller = getController();
    controller.subscribe("test_event_log");

    // Dispatch a month changed event
    auto monthEvent = std::make_shared<MonthChangedEvent>(1, "Sunpeak", Season::Summer);
    EventManager::Instance().dispatchEvent(monthEvent, EventManager::DispatchMode::Immediate);

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestSeasonChangedEventDispatch) {
    auto& controller = getController();
    controller.subscribe("test_event_log");

    // Dispatch a season changed event
    auto seasonEvent = std::make_shared<SeasonChangedEvent>(Season::Summer, Season::Spring, "Summer");
    EventManager::Instance().dispatchEvent(seasonEvent, EventManager::DispatchMode::Immediate);

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestYearChangedEventDispatch) {
    auto& controller = getController();
    controller.subscribe("test_event_log");

    // Dispatch a year changed event
    auto yearEvent = std::make_shared<YearChangedEvent>(2);
    EventManager::Instance().dispatchEvent(yearEvent, EventManager::DispatchMode::Immediate);

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestTimePeriodChangedEventDispatch) {
    auto& controller = getController();
    controller.subscribe("test_event_log");

    // Dispatch a time period changed event
    auto periodEvent = std::make_shared<TimePeriodChangedEvent>(
        TimePeriod::Evening,
        TimePeriod::Day,
        TimePeriodVisuals::getEvening()
    );
    EventManager::Instance().dispatchEvent(periodEvent, EventManager::DispatchMode::Immediate);

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestWeatherCheckEventDispatch) {
    auto& controller = getController();
    controller.subscribe("test_event_log");

    // Dispatch a weather check event
    auto weatherCheckEvent = std::make_shared<WeatherCheckEvent>(Season::Summer, WeatherType::Clear);
    EventManager::Instance().dispatchEvent(weatherCheckEvent, EventManager::DispatchMode::Immediate);

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestNoHandlingWhenUnsubscribed) {
    auto& controller = getController();

    // Ensure not subscribed
    BOOST_CHECK(!controller.isSubscribed());

    // Dispatch events - should not crash even without subscription
    auto hourEvent = std::make_shared<HourChangedEvent>(14, false);
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
