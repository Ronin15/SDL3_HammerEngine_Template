/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE WeatherEventTest
#include <boost/test/unit_test.hpp>

#include "events/WeatherEvent.hpp"
#include <memory>
#include <string>
#include <functional>
#include <iostream>

// Simple test fixture for WeatherEvent
struct WeatherEventFixture {
    WeatherEventFixture() {
        // Setup common test environment
    }

    ~WeatherEventFixture() {
        // Cleanup after tests
    }
};

// Test basic creation and properties
BOOST_FIXTURE_TEST_CASE(BasicProperties, WeatherEventFixture) {
    // Create a weather event with standard type
    auto rainEvent = std::make_shared<WeatherEvent>("Rain", WeatherType::Rainy);

    // Check basic properties
    BOOST_CHECK_EQUAL(rainEvent->getName(), "Rain");
    BOOST_CHECK_EQUAL(rainEvent->getType(), "Weather");
    BOOST_CHECK_EQUAL(rainEvent->getWeatherType(), WeatherType::Rainy);
    BOOST_CHECK_EQUAL(rainEvent->getWeatherTypeString(), "Rainy");
    BOOST_CHECK(rainEvent->isActive());

    // Check default parameters for rainy weather
    BOOST_CHECK_GT(rainEvent->getWeatherParams().intensity, 0.0f);
    BOOST_CHECK_LT(rainEvent->getWeatherParams().visibility, 1.0f);

    // Create a weather event with custom type
    auto customWeather = std::make_shared<WeatherEvent>("Custom", "AcidRain");
    BOOST_CHECK_EQUAL(customWeather->getWeatherType(), WeatherType::Custom);
    BOOST_CHECK_EQUAL(customWeather->getWeatherTypeString(), "AcidRain");
}

// Test weather parameters
BOOST_FIXTURE_TEST_CASE(WeatherParameters, WeatherEventFixture) {
    auto weatherEvent = std::make_shared<WeatherEvent>("Test", WeatherType::Cloudy);

    // Set custom weather parameters
    WeatherParams params;
    params.intensity = 0.8f;
    params.visibility = 0.5f;
    params.transitionTime = 3.0f;
    params.particleEffect = "clouds";
    params.soundEffect = "wind_sound";

    weatherEvent->setWeatherParams(params);

    // Verify parameters were set correctly
    BOOST_CHECK_EQUAL(weatherEvent->getWeatherParams().intensity, 0.8f);
    BOOST_CHECK_EQUAL(weatherEvent->getWeatherParams().visibility, 0.5f);
    BOOST_CHECK_EQUAL(weatherEvent->getWeatherParams().transitionTime, 3.0f);
    BOOST_CHECK_EQUAL(weatherEvent->getWeatherParams().particleEffect, "clouds");
    BOOST_CHECK_EQUAL(weatherEvent->getWeatherParams().soundEffect, "wind_sound");
}

// Test condition handling
BOOST_FIXTURE_TEST_CASE(ConditionHandling, WeatherEventFixture) {
    // Create a new event for condition testing
    auto event = std::make_shared<WeatherEvent>("ConditionTest", WeatherType::Clear);

    // With no conditions, checkConditions should return false
    BOOST_CHECK(!event->checkConditions());

    // Add a condition that always returns true
    event->addTimeCondition([]() { return true; });
    BOOST_CHECK(event->checkConditions());

    // Create a new event to test false conditions
    auto falseEvent = std::make_shared<WeatherEvent>("FalseCondition", WeatherType::Clear);
    falseEvent->addTimeCondition([]() { return false; });
    BOOST_CHECK(!falseEvent->checkConditions());

    // Test multiple conditions (all must pass)
    auto multiEvent = std::make_shared<WeatherEvent>("MultiCondition", WeatherType::Clear);
    multiEvent->addTimeCondition([]() { return true; });
    multiEvent->addTimeCondition([]() { return true; });
    BOOST_CHECK(multiEvent->checkConditions());

    // If any condition fails, the check should fail
    auto mixedEvent = std::make_shared<WeatherEvent>("MixedCondition", WeatherType::Clear);
    mixedEvent->addTimeCondition([]() { return true; });
    mixedEvent->addTimeCondition([]() { return false; });
    BOOST_CHECK(!mixedEvent->checkConditions());
}

// Test time-based conditions
BOOST_FIXTURE_TEST_CASE(TimeBasedConditions, WeatherEventFixture) {
    auto event = std::make_shared<WeatherEvent>("TimeTest", WeatherType::Clear);

    // Set time of day condition (we can't test actual time here, just the API)
    event->setTimeOfDay(8.0f, 16.0f); // Day time only

    // Since we can't control the system time in tests, we can't directly test
    // the result, but we can test that the API works
    bool result = event->checkConditions();
    std::cout << "Time condition result: " << (result ? "true" : "false") << std::endl;

    // This is a weak test, but it at least verifies the API works
    // The actual result will depend on the current time
}

// Test reset and clean
BOOST_FIXTURE_TEST_CASE(ResetAndClean, WeatherEventFixture) {
    auto event = std::make_shared<WeatherEvent>("ResetTest", WeatherType::Rainy);

    // Add a condition
    event->addTimeCondition([]() { return true; });
    BOOST_CHECK(event->checkConditions());

    // Clean should remove all conditions
    event->clean();
    BOOST_CHECK(!event->checkConditions());

    // Test reset (shouldn't affect conditions)
    auto anotherEvent = std::make_shared<WeatherEvent>("AnotherTest", WeatherType::Rainy);
    anotherEvent->addTimeCondition([]() { return true; });
    anotherEvent->reset();
    BOOST_CHECK(anotherEvent->checkConditions());
}

// Test event execution
BOOST_FIXTURE_TEST_CASE(EventExecution, WeatherEventFixture) {
    auto event = std::make_shared<WeatherEvent>("ExecutionTest", WeatherType::Stormy);

    // Set some parameters
    WeatherParams params;
    params.intensity = 1.0f;
    params.particleEffect = "lightning";
    params.soundEffect = "thunder";
    event->setWeatherParams(params);

    // Execute the event (this should output to console but not crash)
    event->execute();

    // Check that the event is marked as triggered
    BOOST_CHECK(event->hasTriggered());
}
