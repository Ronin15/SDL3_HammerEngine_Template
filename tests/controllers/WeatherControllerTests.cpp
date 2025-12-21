/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE WeatherControllerTests
#include <boost/test/unit_test.hpp>

#include "controllers/world/WeatherController.hpp"
#include "managers/GameTimeManager.hpp"
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
        GameTimeManager::Instance().init(12.0f, 1.0f);
    }

    ~WeatherControllerTestFixture() {
        // Controller auto-unsubscribes on destruction via ControllerBase
        // Clean up
        EventManager::Instance().clean();
    }

protected:
    // Controller owned by fixture (new ownership model)
    WeatherController m_controller;
};

// ============================================================================
// OWNERSHIP MODEL TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(OwnershipModelTests)

BOOST_AUTO_TEST_CASE(TestControllerInstantiation) {
    // Controllers can now be instantiated directly
    WeatherController controller1;
    WeatherController controller2;

    // Each is a separate instance
    BOOST_CHECK(&controller1 != &controller2);
}

BOOST_AUTO_TEST_CASE(TestMoveSemantics) {
    WeatherController controller1;
    controller1.subscribe();
    BOOST_CHECK(controller1.isSubscribed());

    // Move constructor
    WeatherController controller2(std::move(controller1));
    BOOST_CHECK(controller2.isSubscribed());
    BOOST_CHECK(!controller1.isSubscribed());  // Moved-from is unsubscribed
}

BOOST_AUTO_TEST_CASE(TestAutoUnsubscribeOnDestruction) {
    {
        WeatherController controller;
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

BOOST_FIXTURE_TEST_SUITE(SubscriptionTests, WeatherControllerTestFixture)

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
// CURRENT WEATHER TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(CurrentWeatherTests, WeatherControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestGetCurrentWeatherDefault) {
    // Default weather should be Clear
    WeatherType weather = m_controller.getCurrentWeather();
    BOOST_CHECK(weather == WeatherType::Clear);
}

BOOST_AUTO_TEST_CASE(TestGetCurrentWeatherString) {
    // Default weather string should be "Clear"
    std::string_view weatherStr = m_controller.getCurrentWeatherString();
    BOOST_CHECK_EQUAL(weatherStr, "Clear");
}

BOOST_AUTO_TEST_CASE(TestWeatherStringValidity) {
    // Weather string should not be empty
    std::string_view weatherStr = m_controller.getCurrentWeatherString();
    BOOST_CHECK(!weatherStr.empty());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// WEATHER CHECK EVENT HANDLING TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(WeatherCheckEventTests, WeatherControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestWeatherCheckEventDispatch) {
    m_controller.subscribe();

    // Dispatch a weather check event with Rainy recommendation
    auto weatherCheckEvent = std::make_shared<WeatherCheckEvent>(Season::Spring, WeatherType::Rainy);
    EventManager::Instance().dispatchEvent(weatherCheckEvent, EventManager::DispatchMode::Immediate);

    // After processing, current weather should update
    BOOST_CHECK(m_controller.getCurrentWeather() == WeatherType::Rainy);
    BOOST_CHECK_EQUAL(std::string(m_controller.getCurrentWeatherString()), "Rainy");
}

BOOST_AUTO_TEST_CASE(TestWeatherCheckEventIgnoredWhenUnsubscribed) {
    // Ensure not subscribed
    BOOST_CHECK(!m_controller.isSubscribed());

    // Get initial weather
    WeatherType initialWeather = m_controller.getCurrentWeather();

    // Dispatch a weather check event
    auto weatherCheckEvent = std::make_shared<WeatherCheckEvent>(Season::Winter, WeatherType::Snowy);
    EventManager::Instance().dispatchEvent(weatherCheckEvent, EventManager::DispatchMode::Immediate);

    // Weather should NOT change since we're not subscribed
    BOOST_CHECK(m_controller.getCurrentWeather() == initialWeather);
}

BOOST_AUTO_TEST_CASE(TestWeatherChangeSequence) {
    m_controller.subscribe();

    // Change weather through sequence
    auto event1 = std::make_shared<WeatherCheckEvent>(Season::Summer, WeatherType::Clear);
    EventManager::Instance().dispatchEvent(event1, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(m_controller.getCurrentWeather() == WeatherType::Clear);

    auto event2 = std::make_shared<WeatherCheckEvent>(Season::Summer, WeatherType::Cloudy);
    EventManager::Instance().dispatchEvent(event2, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(m_controller.getCurrentWeather() == WeatherType::Cloudy);

    auto event3 = std::make_shared<WeatherCheckEvent>(Season::Summer, WeatherType::Rainy);
    EventManager::Instance().dispatchEvent(event3, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(m_controller.getCurrentWeather() == WeatherType::Rainy);

    auto event4 = std::make_shared<WeatherCheckEvent>(Season::Summer, WeatherType::Stormy);
    EventManager::Instance().dispatchEvent(event4, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(m_controller.getCurrentWeather() == WeatherType::Stormy);
}

BOOST_AUTO_TEST_CASE(TestWeatherNoChangeOnSameWeather) {
    m_controller.subscribe();

    // Set initial weather
    auto event1 = std::make_shared<WeatherCheckEvent>(Season::Fall, WeatherType::Cloudy);
    EventManager::Instance().dispatchEvent(event1, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(m_controller.getCurrentWeather() == WeatherType::Cloudy);

    // Dispatch same weather - should be ignored (no duplicate events)
    auto event2 = std::make_shared<WeatherCheckEvent>(Season::Fall, WeatherType::Cloudy);
    EventManager::Instance().dispatchEvent(event2, EventManager::DispatchMode::Immediate);

    // Still Cloudy
    BOOST_CHECK(m_controller.getCurrentWeather() == WeatherType::Cloudy);
}

BOOST_AUTO_TEST_CASE(TestAllWeatherTypes) {
    m_controller.subscribe();

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

        BOOST_CHECK(m_controller.getCurrentWeather() == tc.type);
        BOOST_CHECK_EQUAL(std::string(m_controller.getCurrentWeatherString()), tc.expectedString);
    }
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// TIME EVENT FILTERING TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(TimeEventFilteringTests, WeatherControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestIgnoresNonWeatherCheckTimeEvents) {
    m_controller.subscribe();

    // Get initial weather
    WeatherType initialWeather = m_controller.getCurrentWeather();

    // Dispatch various time events (not WeatherCheckEvent)
    auto hourEvent = std::make_shared<HourChangedEvent>(14, false);
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    auto dayEvent = std::make_shared<DayChangedEvent>(5, 5, 0, "Bloomtide");
    EventManager::Instance().dispatchEvent(dayEvent, EventManager::DispatchMode::Immediate);

    auto seasonEvent = std::make_shared<SeasonChangedEvent>(Season::Summer, Season::Spring, "Summer");
    EventManager::Instance().dispatchEvent(seasonEvent, EventManager::DispatchMode::Immediate);

    // Weather should remain unchanged
    BOOST_CHECK(m_controller.getCurrentWeather() == initialWeather);
}

BOOST_AUTO_TEST_CASE(TestOnlyHandlesWeatherCheckEvent) {
    m_controller.subscribe();

    // Set initial weather
    auto weatherEvent = std::make_shared<WeatherCheckEvent>(Season::Fall, WeatherType::Foggy);
    EventManager::Instance().dispatchEvent(weatherEvent, EventManager::DispatchMode::Immediate);
    BOOST_CHECK(m_controller.getCurrentWeather() == WeatherType::Foggy);

    // Dispatch other time events - should not affect weather
    auto hourEvent = std::make_shared<HourChangedEvent>(8, false);
    EventManager::Instance().dispatchEvent(hourEvent, EventManager::DispatchMode::Immediate);

    // Weather still Foggy
    BOOST_CHECK(m_controller.getCurrentWeather() == WeatherType::Foggy);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// WEATHER DESCRIPTION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(WeatherDescriptionTests, WeatherControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestGetCurrentWeatherDescriptionClear) {
    m_controller.subscribe();

    auto event = std::make_shared<WeatherCheckEvent>(Season::Summer, WeatherType::Clear);
    EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Immediate);

    BOOST_CHECK(m_controller.getCurrentWeather() == WeatherType::Clear);
    BOOST_CHECK_EQUAL(std::string(m_controller.getCurrentWeatherDescription()), "Clear skies");
}

BOOST_AUTO_TEST_CASE(TestGetCurrentWeatherDescriptionCloudy) {
    m_controller.subscribe();

    auto event = std::make_shared<WeatherCheckEvent>(Season::Spring, WeatherType::Cloudy);
    EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Immediate);

    BOOST_CHECK(m_controller.getCurrentWeather() == WeatherType::Cloudy);
    BOOST_CHECK_EQUAL(std::string(m_controller.getCurrentWeatherDescription()), "Clouds gather");
}

BOOST_AUTO_TEST_CASE(TestGetCurrentWeatherDescriptionRainy) {
    m_controller.subscribe();

    auto event = std::make_shared<WeatherCheckEvent>(Season::Fall, WeatherType::Rainy);
    EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Immediate);

    BOOST_CHECK(m_controller.getCurrentWeather() == WeatherType::Rainy);
    BOOST_CHECK_EQUAL(std::string(m_controller.getCurrentWeatherDescription()), "Rain begins");
}

BOOST_AUTO_TEST_CASE(TestGetCurrentWeatherDescriptionStormy) {
    m_controller.subscribe();

    auto event = std::make_shared<WeatherCheckEvent>(Season::Summer, WeatherType::Stormy);
    EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Immediate);

    BOOST_CHECK(m_controller.getCurrentWeather() == WeatherType::Stormy);
    BOOST_CHECK_EQUAL(std::string(m_controller.getCurrentWeatherDescription()), "Storm approaches");
}

BOOST_AUTO_TEST_CASE(TestGetCurrentWeatherDescriptionFoggy) {
    m_controller.subscribe();

    auto event = std::make_shared<WeatherCheckEvent>(Season::Fall, WeatherType::Foggy);
    EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Immediate);

    BOOST_CHECK(m_controller.getCurrentWeather() == WeatherType::Foggy);
    BOOST_CHECK_EQUAL(std::string(m_controller.getCurrentWeatherDescription()), "Fog rolls in");
}

BOOST_AUTO_TEST_CASE(TestGetCurrentWeatherDescriptionSnowy) {
    m_controller.subscribe();

    auto event = std::make_shared<WeatherCheckEvent>(Season::Winter, WeatherType::Snowy);
    EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Immediate);

    BOOST_CHECK(m_controller.getCurrentWeather() == WeatherType::Snowy);
    BOOST_CHECK_EQUAL(std::string(m_controller.getCurrentWeatherDescription()), "Snow falls");
}

BOOST_AUTO_TEST_CASE(TestGetCurrentWeatherDescriptionWindy) {
    m_controller.subscribe();

    auto event = std::make_shared<WeatherCheckEvent>(Season::Spring, WeatherType::Windy);
    EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Immediate);

    BOOST_CHECK(m_controller.getCurrentWeather() == WeatherType::Windy);
    BOOST_CHECK_EQUAL(std::string(m_controller.getCurrentWeatherDescription()), "Wind picks up");
}

BOOST_AUTO_TEST_CASE(TestAllWeatherDescriptions) {
    m_controller.subscribe();

    // Test all weather types have correct descriptions
    struct WeatherDescTestCase {
        WeatherType type;
        const char* expectedDesc;
    };

    WeatherDescTestCase testCases[] = {
        {WeatherType::Clear,  "Clear skies"},
        {WeatherType::Cloudy, "Clouds gather"},
        {WeatherType::Rainy,  "Rain begins"},
        {WeatherType::Stormy, "Storm approaches"},
        {WeatherType::Foggy,  "Fog rolls in"},
        {WeatherType::Snowy,  "Snow falls"},
        {WeatherType::Windy,  "Wind picks up"}
    };

    for (const auto& tc : testCases) {
        auto event = std::make_shared<WeatherCheckEvent>(Season::Spring, tc.type);
        EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Immediate);

        BOOST_CHECK(m_controller.getCurrentWeather() == tc.type);
        BOOST_CHECK_EQUAL(std::string(m_controller.getCurrentWeatherDescription()), tc.expectedDesc);
    }
}

BOOST_AUTO_TEST_SUITE_END()
