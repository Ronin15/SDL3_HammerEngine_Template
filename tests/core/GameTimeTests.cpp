/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE GameTimeTests
#include <boost/test/unit_test.hpp>

#include "core/GameTime.hpp"
#include <cmath>
#include <string>

// Test tolerance for floating-point comparisons
constexpr float EPSILON = 0.001f;

// Helper to check if two floats are approximately equal
bool approxEqual(float a, float b, float epsilon = EPSILON) {
    return std::abs(a - b) < epsilon;
}

// ============================================================================
// Test Fixture
// ============================================================================

class GameTimeTestFixture {
public:
    GameTimeTestFixture() {
        // Get the singleton instance and initialize with default values
        gameTime = &GameTime::Instance();
        gameTime->init(12.0f, 1.0f);  // Start at noon, normal time scale
    }

    ~GameTimeTestFixture() {
        // Reset to known state for next test
        gameTime->resume();  // Ensure not paused
        gameTime->init(12.0f, 1.0f);  // Reset to defaults
    }

protected:
    GameTime* gameTime;
};

// ============================================================================
// SINGLETON PATTERN TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(SingletonTests)

BOOST_AUTO_TEST_CASE(TestSingletonPattern) {
    GameTime* instance1 = &GameTime::Instance();
    GameTime* instance2 = &GameTime::Instance();

    BOOST_CHECK(instance1 == instance2);
    BOOST_CHECK(instance1 != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// INITIALIZATION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(InitializationTests, GameTimeTestFixture)

BOOST_AUTO_TEST_CASE(TestInitializationWithDefaults) {
    // Re-init with defaults
    bool result = gameTime->init(12.0f, 1.0f);

    BOOST_CHECK(result);
    BOOST_CHECK(approxEqual(gameTime->getGameHour(), 12.0f));
    BOOST_CHECK_EQUAL(gameTime->getGameDay(), 1);
    BOOST_CHECK(approxEqual(gameTime->getTimeScale(), 1.0f));
}

BOOST_AUTO_TEST_CASE(TestInitializationWithCustomValues) {
    // Init with custom start hour and time scale
    bool result = gameTime->init(6.0f, 2.0f);

    BOOST_CHECK(result);
    BOOST_CHECK(approxEqual(gameTime->getGameHour(), 6.0f));
    BOOST_CHECK(approxEqual(gameTime->getTimeScale(), 2.0f));
}

BOOST_AUTO_TEST_CASE(TestInitializationWithInvalidHour) {
    // Negative hour should fail
    bool result1 = gameTime->init(-1.0f, 1.0f);
    BOOST_CHECK(!result1);

    // Hour >= 24 should fail
    bool result2 = gameTime->init(24.0f, 1.0f);
    BOOST_CHECK(!result2);

    bool result3 = gameTime->init(25.0f, 1.0f);
    BOOST_CHECK(!result3);
}

BOOST_AUTO_TEST_CASE(TestInitializationWithInvalidTimeScale) {
    // Zero time scale should fail
    bool result1 = gameTime->init(12.0f, 0.0f);
    BOOST_CHECK(!result1);

    // Negative time scale should fail
    bool result2 = gameTime->init(12.0f, -1.0f);
    BOOST_CHECK(!result2);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// TIME PROGRESSION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(TimeProgressionTests, GameTimeTestFixture)

BOOST_AUTO_TEST_CASE(TestTimeProgression) {
    // Init at midnight
    gameTime->init(0.0f, 1.0f);
    float initialHour = gameTime->getGameHour();

    // Update with 1 hour of game time (3600 seconds at 1.0 time scale)
    gameTime->update(3600.0f);

    float newHour = gameTime->getGameHour();
    BOOST_CHECK(approxEqual(newHour, initialHour + 1.0f));
}

BOOST_AUTO_TEST_CASE(TestTimeProgressionWithScale) {
    // Init at midnight with 2x time scale
    gameTime->init(0.0f, 2.0f);
    float initialHour = gameTime->getGameHour();

    // Update with 1 real hour (3600 real seconds = 2 game hours at 2x scale)
    gameTime->update(3600.0f);

    float newHour = gameTime->getGameHour();
    BOOST_CHECK(approxEqual(newHour, initialHour + 2.0f));
}

BOOST_AUTO_TEST_CASE(TestDayProgression) {
    // Init at 23:00
    gameTime->init(23.0f, 1.0f);
    int initialDay = gameTime->getGameDay();

    // Update with 2 hours (past midnight)
    gameTime->update(7200.0f);

    int newDay = gameTime->getGameDay();
    float newHour = gameTime->getGameHour();

    BOOST_CHECK_EQUAL(newDay, initialDay + 1);
    BOOST_CHECK(approxEqual(newHour, 1.0f));
}

BOOST_AUTO_TEST_CASE(TestTimeScaleChange) {
    gameTime->init(12.0f, 1.0f);
    BOOST_CHECK(approxEqual(gameTime->getTimeScale(), 1.0f));

    gameTime->setTimeScale(5.0f);
    BOOST_CHECK(approxEqual(gameTime->getTimeScale(), 5.0f));

    gameTime->setTimeScale(0.5f);
    BOOST_CHECK(approxEqual(gameTime->getTimeScale(), 0.5f));
}

BOOST_AUTO_TEST_CASE(TestTotalGameTimeSeconds) {
    gameTime->init(12.0f, 1.0f);
    float initialSeconds = gameTime->getTotalGameTimeSeconds();

    // 12 hours = 43200 seconds
    BOOST_CHECK(approxEqual(initialSeconds, 43200.0f));

    // Update with 1 hour
    gameTime->update(3600.0f);
    float newSeconds = gameTime->getTotalGameTimeSeconds();

    BOOST_CHECK(approxEqual(newSeconds, initialSeconds + 3600.0f));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// PAUSE/RESUME TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(PauseResumeTests, GameTimeTestFixture)

BOOST_AUTO_TEST_CASE(TestPauseResume) {
    gameTime->init(12.0f, 1.0f);

    // Initially not paused
    BOOST_CHECK(!gameTime->isPaused());

    // Pause
    gameTime->pause();
    BOOST_CHECK(gameTime->isPaused());

    // Resume
    gameTime->resume();
    BOOST_CHECK(!gameTime->isPaused());
}

BOOST_AUTO_TEST_CASE(TestUpdateWhilePaused) {
    gameTime->init(12.0f, 1.0f);
    float initialHour = gameTime->getGameHour();

    // Pause and update
    gameTime->pause();
    gameTime->update(3600.0f);

    // Time should not have advanced
    float newHour = gameTime->getGameHour();
    BOOST_CHECK(approxEqual(newHour, initialHour));
}

BOOST_AUTO_TEST_CASE(TestResumeAfterPause) {
    gameTime->init(12.0f, 1.0f);

    // Pause, then resume
    gameTime->pause();
    gameTime->resume();

    float initialHour = gameTime->getGameHour();

    // Update should work after resume
    gameTime->update(3600.0f);

    float newHour = gameTime->getGameHour();
    BOOST_CHECK(approxEqual(newHour, initialHour + 1.0f));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// DAYTIME/NIGHTTIME TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(DaytimeNighttimeTests, GameTimeTestFixture)

BOOST_AUTO_TEST_CASE(TestDaytimeDetection) {
    // Set to noon - should be daytime
    gameTime->init(12.0f, 1.0f);
    BOOST_CHECK(gameTime->isDaytime());
    BOOST_CHECK(!gameTime->isNighttime());

    // Set to 8 AM - should be daytime
    gameTime->init(8.0f, 1.0f);
    BOOST_CHECK(gameTime->isDaytime());
    BOOST_CHECK(!gameTime->isNighttime());
}

BOOST_AUTO_TEST_CASE(TestNighttimeDetection) {
    // Set to midnight - should be nighttime
    gameTime->init(0.0f, 1.0f);
    BOOST_CHECK(!gameTime->isDaytime());
    BOOST_CHECK(gameTime->isNighttime());

    // Set to 3 AM - should be nighttime
    gameTime->init(3.0f, 1.0f);
    BOOST_CHECK(!gameTime->isDaytime());
    BOOST_CHECK(gameTime->isNighttime());

    // Set to 22:00 (10 PM) - should be nighttime
    gameTime->init(22.0f, 1.0f);
    BOOST_CHECK(!gameTime->isDaytime());
    BOOST_CHECK(gameTime->isNighttime());
}

BOOST_AUTO_TEST_CASE(TestCustomDaylightHours) {
    gameTime->init(12.0f, 1.0f);

    // Set custom daylight hours: 8 AM to 6 PM
    gameTime->setDaylightHours(8.0f, 18.0f);

    // 7 AM should be nighttime with these settings
    gameTime->setGameHour(7.0f);
    BOOST_CHECK(gameTime->isNighttime());

    // 9 AM should be daytime
    gameTime->setGameHour(9.0f);
    BOOST_CHECK(gameTime->isDaytime());

    // 7 PM (19:00) should be nighttime
    gameTime->setGameHour(19.0f);
    BOOST_CHECK(gameTime->isNighttime());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// TIME OF DAY NAME TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(TimeOfDayNameTests, GameTimeTestFixture)

BOOST_AUTO_TEST_CASE(TestTimeOfDayName) {
    // Morning: 5:00 - 8:00
    gameTime->init(6.0f, 1.0f);
    BOOST_CHECK_EQUAL(std::string(gameTime->getTimeOfDayName()), "Morning");

    // Day: 8:00 - 17:00
    gameTime->init(12.0f, 1.0f);
    BOOST_CHECK_EQUAL(std::string(gameTime->getTimeOfDayName()), "Day");

    // Evening: 17:00 - 21:00
    gameTime->init(19.0f, 1.0f);
    BOOST_CHECK_EQUAL(std::string(gameTime->getTimeOfDayName()), "Evening");

    // Night: 21:00 - 5:00
    gameTime->init(23.0f, 1.0f);
    BOOST_CHECK_EQUAL(std::string(gameTime->getTimeOfDayName()), "Night");

    gameTime->init(2.0f, 1.0f);
    BOOST_CHECK_EQUAL(std::string(gameTime->getTimeOfDayName()), "Night");
}

BOOST_AUTO_TEST_CASE(TestTimeOfDayBoundaries) {
    // At 5:00 exactly - should be Morning
    gameTime->init(5.0f, 1.0f);
    BOOST_CHECK_EQUAL(std::string(gameTime->getTimeOfDayName()), "Morning");

    // At 8:00 exactly - should be Day
    gameTime->init(8.0f, 1.0f);
    BOOST_CHECK_EQUAL(std::string(gameTime->getTimeOfDayName()), "Day");

    // At 17:00 exactly - should be Evening
    gameTime->init(17.0f, 1.0f);
    BOOST_CHECK_EQUAL(std::string(gameTime->getTimeOfDayName()), "Evening");

    // At 21:00 exactly - should be Night
    gameTime->init(21.0f, 1.0f);
    BOOST_CHECK_EQUAL(std::string(gameTime->getTimeOfDayName()), "Night");
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// SET GAME HOUR/DAY TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(SetGameHourDayTests, GameTimeTestFixture)

BOOST_AUTO_TEST_CASE(TestSetGameHour) {
    gameTime->init(12.0f, 1.0f);

    // Set to valid hour
    gameTime->setGameHour(18.0f);
    BOOST_CHECK(approxEqual(gameTime->getGameHour(), 18.0f));

    // Set to 0 (midnight)
    gameTime->setGameHour(0.0f);
    BOOST_CHECK(approxEqual(gameTime->getGameHour(), 0.0f));

    // Set to 23.5 (11:30 PM)
    gameTime->setGameHour(23.5f);
    BOOST_CHECK(approxEqual(gameTime->getGameHour(), 23.5f));
}

BOOST_AUTO_TEST_CASE(TestSetGameHourInvalidValues) {
    gameTime->init(12.0f, 1.0f);
    float initialHour = gameTime->getGameHour();

    // Negative hour should be ignored
    gameTime->setGameHour(-1.0f);
    BOOST_CHECK(approxEqual(gameTime->getGameHour(), initialHour));

    // Hour >= 24 should be ignored
    gameTime->setGameHour(24.0f);
    BOOST_CHECK(approxEqual(gameTime->getGameHour(), initialHour));
}

BOOST_AUTO_TEST_CASE(TestSetGameDay) {
    gameTime->init(12.0f, 1.0f);

    // Set valid day
    gameTime->setGameDay(5);
    BOOST_CHECK_EQUAL(gameTime->getGameDay(), 5);

    gameTime->setGameDay(100);
    BOOST_CHECK_EQUAL(gameTime->getGameDay(), 100);
}

BOOST_AUTO_TEST_CASE(TestSetGameDayMinimum) {
    gameTime->init(12.0f, 1.0f);

    // Day 0 should be converted to 1
    gameTime->setGameDay(0);
    BOOST_CHECK_EQUAL(gameTime->getGameDay(), 1);

    // Negative day should be converted to 1
    gameTime->setGameDay(-5);
    BOOST_CHECK_EQUAL(gameTime->getGameDay(), 1);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// FORMAT TIME TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(FormatTimeTests, GameTimeTestFixture)

BOOST_AUTO_TEST_CASE(TestFormatCurrentTime24Hour) {
    gameTime->init(14.5f, 1.0f);  // 2:30 PM

    std::string formatted(gameTime->formatCurrentTime(true));
    BOOST_CHECK_EQUAL(formatted, "14:30");
}

BOOST_AUTO_TEST_CASE(TestFormatCurrentTime12Hour) {
    // Test PM time
    gameTime->init(14.5f, 1.0f);  // 2:30 PM
    std::string formatted1(gameTime->formatCurrentTime(false));
    BOOST_CHECK_EQUAL(formatted1, "2:30 PM");

    // Test AM time
    gameTime->init(9.25f, 1.0f);  // 9:15 AM
    std::string formatted2(gameTime->formatCurrentTime(false));
    BOOST_CHECK_EQUAL(formatted2, "9:15 AM");
}

BOOST_AUTO_TEST_CASE(TestFormatCurrentTimeMidnight) {
    gameTime->init(0.0f, 1.0f);  // Midnight

    std::string formatted24(gameTime->formatCurrentTime(true));
    BOOST_CHECK_EQUAL(formatted24, "00:00");

    std::string formatted12(gameTime->formatCurrentTime(false));
    BOOST_CHECK_EQUAL(formatted12, "12:00 AM");
}

BOOST_AUTO_TEST_CASE(TestFormatCurrentTimeNoon) {
    gameTime->init(12.0f, 1.0f);  // Noon

    std::string formatted24(gameTime->formatCurrentTime(true));
    BOOST_CHECK_EQUAL(formatted24, "12:00");

    std::string formatted12(gameTime->formatCurrentTime(false));
    BOOST_CHECK_EQUAL(formatted12, "12:00 PM");
}

BOOST_AUTO_TEST_SUITE_END()
