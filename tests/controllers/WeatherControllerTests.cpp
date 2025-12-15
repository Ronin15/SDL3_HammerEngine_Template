/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE WeatherControllerTests
#include <boost/test/unit_test.hpp>

#include "controllers/world/WeatherController.hpp"
#include "core/GameTime.hpp"
#include "managers/EventManager.hpp"
#include "events/TimeEvent.hpp"
#include "events/WeatherEvent.hpp"
#include "../events/EventManagerTestAccess.hpp"
#include <string>

// ============================================================================
// Test Fixture
// ============================================================================

class WeatherControllerTestFixture {
public:
    WeatherControllerTestFixture() {
        // Reset event manager to clean state
        EventManagerTestAccess::reset();

        // Initialize GameTime
        GameTime::Instance().init(12.0f, 1.0f);
    }

    ~WeatherControllerTestFixture() {
        // Ensure WeatherController is unsubscribed
        WeatherController::Instance().unsubscribe();

        // Clean up
        EventManager::Instance().clean();
    }

protected:
    WeatherController& getController() {
        return WeatherController::Instance();
    }
};

// ============================================================================
// SINGLETON PATTERN TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(SingletonTests)

BOOST_AUTO_TEST_CASE(TestSingletonPattern) {
    WeatherController* instance1 = &WeatherController::Instance();
    WeatherController* instance2 = &WeatherController::Instance();

    BOOST_CHECK(instance1 == instance2);
    BOOST_CHECK(instance1 != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// SUBSCRIPTION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(SubscriptionTests, WeatherControllerTestFixture)

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

    // Second subscribe should be ignored (no crash, still subscribed)
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

    // Second unsubscribe should be safe (no crash)
    controller.unsubscribe();
    BOOST_CHECK(!controller.isSubscribed());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// CURRENT WEATHER TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(CurrentWeatherTests, WeatherControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestGetCurrentWeatherDefault) {
    auto& controller = getController();

    // Default weather should be Clear
    WeatherType weather = controller.getCurrentWeather();
    BOOST_CHECK(weather == WeatherType::Clear);
}

BOOST_AUTO_TEST_CASE(TestGetCurrentWeatherString) {
    auto& controller = getController();

    // Default weather string should be "Clear"
    const char* weatherStr = controller.getCurrentWeatherString();
    BOOST_CHECK_EQUAL(std::string(weatherStr), "Clear");
}

BOOST_AUTO_TEST_CASE(TestWeatherStringValidity) {
    auto& controller = getController();

    // Weather string should not be null
    const char* weatherStr = controller.getCurrentWeatherString();
    BOOST_CHECK(weatherStr != nullptr);

    // Should have some content
    BOOST_CHECK(std::strlen(weatherStr) > 0);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// WEATHER CHECK EVENT HANDLING TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(WeatherCheckEventTests, WeatherControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestWeatherCheckEventDispatch) {
    auto& controller = getController();
    controller.subscribe();

    // Dispatch a weather check event with Rainy recommendation
    auto weatherCheckEvent = std::make_shared<WeatherCheckEvent>(Season::Spring, WeatherType::Rainy);
    EventManager::Instance().dispatchEvent(weatherCheckEvent, EventManager::DispatchMode::Immediate);

    // After processing, current weather should update
    BOOST_CHECK(controller.getCurrentWeather() == WeatherType::Rainy);
    BOOST_CHECK_EQUAL(std::string(controller.getCurrentWeatherString()), "Rainy");
}

BOOST_AUTO_TEST_CASE(TestWeatherCheckEventIgnoredWhenUnsubscribed) {
    auto& controller = getController();

    // Ensure not subscribed
    BOOST_CHECK(!controller.isSubscribed());

    // Get initial weather
    WeatherType initialWeather = controller.getCurrentWeather();

    // Dispatch a weather check event
    auto weatherCheckEvent = std::make_shared<WeatherCheckEvent>(Season::Winter, WeatherType::Snowy);
    EventManager::Instance().dispatchEvent(weatherCheckEvent, EventManager::DispatchMode::Immediate);

    // Weather should NOT change since we're not subscribed
    BOOST_CHECK(controller.getCurrentWeather() == initialWeather);
}

BOOST_AUTO_TEST_CASE(TestWeatherChangeSequence) {
    auto& controller = getController();
    controller.subscribe();

    // Change weather through sequence
    auto event1 = std::make_shared<WeatherCheckEvent>(Season::Summer, WeatherType::Clear);
    EventManager::Instance().dispatchEvent(event1, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(controller.getCurrentWeather() == WeatherType::Clear);

    auto event2 = std::make_shared<WeatherCheckEvent>(Season::Summer, WeatherType::Cloudy);
    EventManager::Instance().dispatchEvent(event2, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(controller.getCurrentWeather() == WeatherType::Cloudy);

    auto event3 = std::make_shared<WeatherCheckEvent>(Season::Summer, WeatherType::Rainy);
    EventManager::Instance().dispatchEvent(event3, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(controller.getCurrentWeather() == WeatherType::Rainy);

    auto event4 = std::make_shared<WeatherCheckEvent>(Season::Summer, WeatherType::Stormy);
    EventManager::Instance().dispatchEvent(event4, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(controller.getCurrentWeather() == WeatherType::Stormy);
}

BOOST_AUTO_TEST_CASE(TestWeatherNoChangeOnSameWeather) {
    auto& controller = getController();
    controller.subscribe();

    // Set initial weather
    auto event1 = std::make_shared<WeatherCheckEvent>(Season::Fall, WeatherType::Cloudy);
    EventManager::Instance().dispatchEvent(event1, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(controller.getCurrentWeather() == WeatherType::Cloudy);

    // Dispatch same weather - should be ignored (no duplicate events)
    auto event2 = std::make_shared<WeatherCheckEvent>(Season::Fall, WeatherType::Cloudy);
    EventManager::Instance().dispatchEvent(event2, EventManager::DispatchMode::Immediate);

    // Still Cloudy
    BOOST_CHECK(controller.getCurrentWeather() == WeatherType::Cloudy);
}

BOOST_AUTO_TEST_CASE(TestAllWeatherTypes) {
    auto& controller = getController();
    controller.subscribe();

    // Test all weather types
    struct WeatherTestCase {
        WeatherType type;
        const char* expectedString;
    };

    WeatherTestCase testCases[] = {
        {WeatherType::Clear, "Clear"},
        {WeatherType::Cloudy, "Cloudy"},
        {WeatherType::Rainy, "Rainy"},
        {WeatherType::Stormy, "Stormy"},
        {WeatherType::Foggy, "Foggy"},
        {WeatherType::Snowy, "Snowy"},
        {WeatherType::Windy, "Windy"}
    };

    for (const auto& tc : testCases) {
        auto event = std::make_shared<WeatherCheckEvent>(Season::Spring, tc.type);
        EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Immediate);

        BOOST_CHECK(controller.getCurrentWeather() == tc.type);
        BOOST_CHECK_EQUAL(std::string(controller.getCurrentWeatherString()), tc.expectedString);
    }
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// TIME EVENT FILTERING TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(TimeEventFilteringTests, WeatherControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestIgnoresNonWeatherCheckTimeEvents) {
    auto& controller = getController();
    controller.subscribe();

    // Get initial weather
    WeatherType initialWeather = controller.getCurrentWeather();

    // Dispatch various time events (not WeatherCheckEvent)
    auto hourEvent = std::make_shared<HourChangedEvent>(14, false);
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    auto dayEvent = std::make_shared<DayChangedEvent>(5, 5, 0, "Bloomtide");
    EventManager::Instance().dispatchEvent(dayEvent, EventManager::DispatchMode::Immediate);

    auto seasonEvent = std::make_shared<SeasonChangedEvent>(Season::Summer, Season::Spring, "Summer");
    EventManager::Instance().dispatchEvent(seasonEvent, EventManager::DispatchMode::Immediate);

    // Weather should remain unchanged
    BOOST_CHECK(controller.getCurrentWeather() == initialWeather);
}

BOOST_AUTO_TEST_CASE(TestOnlyHandlesWeatherCheckEvent) {
    auto& controller = getController();
    controller.subscribe();

    // Set initial weather
    auto weatherEvent = std::make_shared<WeatherCheckEvent>(Season::Fall, WeatherType::Foggy);
    EventManager::Instance().dispatchEvent(weatherEvent, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(controller.getCurrentWeather() == WeatherType::Foggy);

    // Dispatch other time events - should not affect weather
    auto hourEvent = std::make_shared<HourChangedEvent>(8, false);
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    // Weather still Foggy
    BOOST_CHECK(controller.getCurrentWeather() == WeatherType::Foggy);
}

BOOST_AUTO_TEST_SUITE_END()
