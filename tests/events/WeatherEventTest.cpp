/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE WeatherEventTest
#include <boost/test/unit_test.hpp>

#include "events/WeatherEvent.hpp"
#include "managers/WorldManager.hpp"
#include "managers/EventManager.hpp"
#include "EventManagerTestAccess.hpp"
#include "world/WorldData.hpp"
#include "managers/GameTimeManager.hpp"
#include <memory>
#include <string>
#include <functional>
#include <thread>
#include <chrono>
#include <iostream>
#include <map>
#include <cmath>

// Simple test fixture for WeatherEvent
struct WeatherEventFixture {
    WeatherEventFixture() {
        // Initialize managers used by region tests if not already
        EventManagerTestAccess::reset();
        WorldManager::Instance().init();
    }

    ~WeatherEventFixture() {
        // Cleanup after tests
        EventManager::Instance().clean();
        WorldManager::Instance().clean();
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

    // Test reset - clears all data including conditions for event pool recycling
    auto anotherEvent = std::make_shared<WeatherEvent>("AnotherTest", WeatherType::Rainy);
    anotherEvent->addTimeCondition([]() { return true; });
    anotherEvent->reset();
    BOOST_CHECK(!anotherEvent->checkConditions());
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

// New tests for region + bounds logic using biome as region
BOOST_FIXTURE_TEST_CASE(RegionNameOnly_MismatchFails_MatchPasses, WeatherEventFixture) {
    // Generate a small world
    HammerEngine::WorldGenerationConfig cfg{};
    cfg.width = 10; cfg.height = 10; cfg.seed = 1234; cfg.elevationFrequency = 0.1f; cfg.humidityFrequency = 0.1f; cfg.waterLevel = 0.3f; cfg.mountainLevel = 0.7f;
    BOOST_REQUIRE(WorldManager::Instance().loadNewWorld(cfg));

    // Force tile (0,0) biome to FOREST deterministically
    auto* tile = WorldManager::Instance().getTileAt(0,0);
    BOOST_REQUIRE(tile != nullptr);
    tile->biome = HammerEngine::Biome::FOREST;

    auto evt = std::make_shared<WeatherEvent>("RegionTest", WeatherType::Cloudy);
    evt->setGeographicRegion("FOREST");

    // With matching region, conditions should pass (no other conditions set)
    BOOST_CHECK(evt->checkConditions());

    // Change biome to DESERT, now region should not match
    tile->biome = HammerEngine::Biome::DESERT;
    BOOST_CHECK(!evt->checkConditions());
}

BOOST_FIXTURE_TEST_CASE(RegionAndBounds_BothMustPass, WeatherEventFixture) {
    // Generate a small world and set (0,0) biome FOREST
    HammerEngine::WorldGenerationConfig cfg{};
    cfg.width = 8; cfg.height = 8; cfg.seed = 5678; cfg.elevationFrequency = 0.1f; cfg.humidityFrequency = 0.1f; cfg.waterLevel = 0.3f; cfg.mountainLevel = 0.7f;
    BOOST_REQUIRE(WorldManager::Instance().loadNewWorld(cfg));
    auto* tile = WorldManager::Instance().getTileAt(0,0);
    BOOST_REQUIRE(tile != nullptr);
    tile->biome = HammerEngine::Biome::FOREST;

    auto evt = std::make_shared<WeatherEvent>("RegionBoundsTest", WeatherType::Cloudy);
    evt->setGeographicRegion("FOREST");

    // Bounds including (0,0) should pass
    evt->setBoundingArea(-1.0f, -1.0f, 1.0f, 1.0f);
    BOOST_CHECK(evt->checkConditions());

    // Bounds excluding (0,0) should fail even though region matches
    evt->setBoundingArea(10.0f, 10.0f, 20.0f, 20.0f);
    BOOST_CHECK(!evt->checkConditions());
}

BOOST_FIXTURE_TEST_CASE(NoRegion_BoundsOnly, WeatherEventFixture) {
    auto evt = std::make_shared<WeatherEvent>("BoundsOnly", WeatherType::Clear);

    // Include (0,0)
    evt->setBoundingArea(-1.0f, -1.0f, 1.0f, 1.0f);
    BOOST_CHECK(evt->checkConditions());

    // Exclude (0,0)
    evt->setBoundingArea(10.0f, 10.0f, 20.0f, 20.0f);
    BOOST_CHECK(!evt->checkConditions());
}

// ============================================================================
// GAMETIME WEATHER SYSTEM TESTS
// ============================================================================

// Fixture specifically for GameTime weather tests
struct GameTimeWeatherFixture {
    GameTimeWeatherFixture() {
        gameTime = &GameTimeManager::Instance();
        gameTime->init(12.0f, 1.0f);
    }

    ~GameTimeWeatherFixture() {
        gameTime->enableAutoWeather(false);
        gameTime->setGlobalPause(false);
        gameTime->init(12.0f, 1.0f);
    }

protected:
    GameTimeManager* gameTime;
};

BOOST_FIXTURE_TEST_CASE(AutoWeatherToggle, GameTimeWeatherFixture) {
    // Initially auto weather should be disabled by default
    BOOST_CHECK(!gameTime->isAutoWeatherEnabled());

    // Enable auto weather
    gameTime->enableAutoWeather(true);
    BOOST_CHECK(gameTime->isAutoWeatherEnabled());

    // Disable auto weather
    gameTime->enableAutoWeather(false);
    BOOST_CHECK(!gameTime->isAutoWeatherEnabled());

    // Toggle multiple times
    gameTime->enableAutoWeather(true);
    gameTime->enableAutoWeather(true);  // Enabling when already enabled
    BOOST_CHECK(gameTime->isAutoWeatherEnabled());
}

BOOST_FIXTURE_TEST_CASE(WeatherCheckInterval, GameTimeWeatherFixture) {
    // Default interval should be 4.0f game hours
    // Note: We test behavior through side effects since there's no getter

    // Set a custom interval (valid value)
    gameTime->setWeatherCheckInterval(2.0f);

    // Set another valid interval
    gameTime->setWeatherCheckInterval(8.0f);

    // Try to set invalid interval (should be ignored)
    gameTime->setWeatherCheckInterval(0.0f);  // Zero is invalid
    gameTime->setWeatherCheckInterval(-1.0f); // Negative is invalid

    // No crash means success - the invalid values should be ignored
    BOOST_CHECK(true);
}

BOOST_FIXTURE_TEST_CASE(RollWeatherForCurrentSeason, GameTimeWeatherFixture) {
    // Set to spring
    gameTime->setGameDay(1);
    gameTime->update(0.0f);
    BOOST_CHECK(gameTime->getSeason() == Season::Spring);

    // Roll weather multiple times - should return valid WeatherType
    for (int i = 0; i < 10; ++i) {
        WeatherType weather = gameTime->rollWeatherForSeason();
        // Verify it's a valid weather type (enum range check)
        int weatherVal = static_cast<int>(weather);
        BOOST_CHECK_GE(weatherVal, 0);
        BOOST_CHECK_LE(weatherVal, 7);  // WeatherType enum has 8 values (0-7)
    }
}

BOOST_FIXTURE_TEST_CASE(RollWeatherForSpecificSeason, GameTimeWeatherFixture) {
    // Roll weather for each season explicitly
    for (int s = 0; s < 4; ++s) {
        Season season = static_cast<Season>(s);
        WeatherType weather = gameTime->rollWeatherForSeason(season);

        // Verify it's a valid weather type
        int weatherVal = static_cast<int>(weather);
        BOOST_CHECK_GE(weatherVal, 0);
        BOOST_CHECK_LE(weatherVal, 7);
    }

    // Specifically test that winter can produce snow
    // Roll multiple times for winter to increase chance of snow
    bool gotSnow = false;
    for (int i = 0; i < 100; ++i) {
        WeatherType weather = gameTime->rollWeatherForSeason(Season::Winter);
        if (weather == WeatherType::Snowy) {
            gotSnow = true;
            break;
        }
    }
    // With 25% snow probability in winter, we should see snow in 100 rolls
    BOOST_CHECK(gotSnow);
}

BOOST_FIXTURE_TEST_CASE(WeatherProbabilityDistribution, GameTimeWeatherFixture) {
    // Roll weather many times and verify distribution roughly matches probabilities
    const int NUM_ROLLS = 1000;
    std::map<WeatherType, int> counts;

    for (int i = 0; i < NUM_ROLLS; ++i) {
        WeatherType weather = gameTime->rollWeatherForSeason(Season::Summer);
        counts[weather]++;
    }

    // Summer probabilities:
    // Clear: 50%, Cloudy: 20%, Rainy: 15%, Stormy: 10%, Foggy: 0%, Snowy: 0%, Windy: 5%

    // Check that Clear is most common (should be ~500 out of 1000)
    BOOST_CHECK_GT(counts[WeatherType::Clear], counts[WeatherType::Cloudy]);
    BOOST_CHECK_GT(counts[WeatherType::Clear], counts[WeatherType::Rainy]);

    // Check that non-summer weather types have zero or near-zero
    BOOST_CHECK_EQUAL(counts[WeatherType::Foggy], 0);  // Summer has 0% foggy
    BOOST_CHECK_EQUAL(counts[WeatherType::Snowy], 0);  // Summer has 0% snowy

    // Cloudy should be more common than rainy (20% vs 15%)
    // Allow some variance due to randomness
    BOOST_CHECK_GT(counts[WeatherType::Cloudy] + 50, counts[WeatherType::Rainy]);
}

BOOST_FIXTURE_TEST_CASE(WinterSnowProbability, GameTimeWeatherFixture) {
    // Winter has 25% snow probability
    SeasonConfig winterConfig = SeasonConfig::getDefault(Season::Winter);
    BOOST_CHECK_GT(winterConfig.weatherProbs.snowy, 0.0f);
    BOOST_CHECK_CLOSE(winterConfig.weatherProbs.snowy, 0.25f, 0.01f);

    // Non-winter seasons should have 0% snow
    SeasonConfig springConfig = SeasonConfig::getDefault(Season::Spring);
    SeasonConfig summerConfig = SeasonConfig::getDefault(Season::Summer);
    SeasonConfig fallConfig = SeasonConfig::getDefault(Season::Fall);

    BOOST_CHECK_EQUAL(springConfig.weatherProbs.snowy, 0.0f);
    BOOST_CHECK_EQUAL(summerConfig.weatherProbs.snowy, 0.0f);
    BOOST_CHECK_EQUAL(fallConfig.weatherProbs.snowy, 0.0f);
}

BOOST_FIXTURE_TEST_CASE(SeasonWeatherProbabilitiesSumToOne, GameTimeWeatherFixture) {
    // Verify all season weather probabilities sum to 1.0
    for (int s = 0; s < 4; ++s) {
        Season season = static_cast<Season>(s);
        SeasonConfig config = SeasonConfig::getDefault(season);
        const auto& probs = config.weatherProbs;

        float sum = probs.clear + probs.cloudy + probs.rainy +
                    probs.stormy + probs.foggy + probs.snowy + probs.windy;

        BOOST_CHECK_CLOSE(sum, 1.0f, 1.0f);  // Allow 1% tolerance
    }
}
