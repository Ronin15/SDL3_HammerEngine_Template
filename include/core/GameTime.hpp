/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef GAME_TIME_HPP
#define GAME_TIME_HPP

/**
 * @file GameTime.hpp
 * @brief Management of game time, including day/night cycles and time scaling
 *
 * The GameTime class provides functionality for:
 * - Tracking real-time vs. game time
 * - Day/night cycles
 * - Time acceleration/deceleration
 * - Time-based events and scheduling
 */

#include <chrono>
#include <string>

class GameTime {
public:
    /**
     * @brief Get the singleton instance of GameTime
     * @return Reference to the GameTime instance
     */
    static GameTime& Instance() {
        static GameTime instance;
        return instance;
    }

    /**
     * @brief Initialize the game time system
     * @param startHour Starting hour of game time (0-23)
     * @param timeScale Scale factor for time progression (1.0 = real time)
     * @return True if initialization succeeded, false otherwise
     */
    bool init(float startHour = 12.0f, float timeScale = 1.0f);

    /**
     * @brief Update game time based on real elapsed time
     * @param deltaTime Real time elapsed in seconds since last update
     */
    void update(float deltaTime);

    /**
     * @brief Get current game hour (0-23.999)
     * @return Current hour including fractional part
     */
    float getGameHour() const { return m_currentHour; }

    /**
     * @brief Get current game day
     * @return Current day number (starts at 1)
     */
    int getGameDay() const { return m_currentDay; }

    /**
     * @brief Get current game season (0=Spring, 1=Summer, 2=Fall, 3=Winter)
     * @param daysPerSeason Number of game days per season (default: 30)
     * @return Current season index (0-3)
     */
    int getCurrentSeason(int daysPerSeason = 30) const;

    /**
     * @brief Get total elapsed game time in seconds
     * @return Total game seconds elapsed
     */
    float getTotalGameTimeSeconds() const { return m_totalGameSeconds; }

    /**
     * @brief Check if it's daytime in the game
     * @return True if current hour is between sunrise and sunset
     */
    bool isDaytime() const;

    /**
     * @brief Check if it's nighttime in the game
     * @return True if current hour is between sunset and sunrise
     */
    bool isNighttime() const;

    /**
     * @brief Get current time of day as string (Morning, Day, Evening, Night)
     * @return String representation of time of day
     */
    std::string getTimeOfDayName() const;

    /**
     * @brief Set the time scale factor
     * @param scale Scale factor (1.0 = real time, 2.0 = twice as fast, etc.)
     */
    void setTimeScale(float scale) { m_timeScale = scale; }

    /**
     * @brief Get the current time scale factor
     * @return Current time scale
     */
    float getTimeScale() const { return m_timeScale; }

    /**
     * @brief Set the current game hour
     * @param hour Hour to set (0-23.999)
     */
    void setGameHour(float hour);

    /**
     * @brief Set the current game day
     * @param day Day to set (must be >= 1)
     */
    void setGameDay(int day) { m_currentDay = (day >= 1) ? day : 1; }

    /**
     * @brief Set sunrise and sunset hours
     * @param sunrise Hour when sun rises (0-23.999)
     * @param sunset Hour when sun sets (0-23.999)
     */
    void setDaylightHours(float sunrise, float sunset);

    /**
     * @brief Format current game time as a string
     * @param use24Hour Whether to use 24-hour format
     * @return Formatted time string (e.g., "14:30" or "2:30 PM")
     */
    std::string formatCurrentTime(bool use24Hour = true) const;

private:
    // Singleton constructor/destructor
    GameTime();
    ~GameTime() = default;

    // Prevent copying
    GameTime(const GameTime&) = delete;
    GameTime& operator=(const GameTime&) = delete;

    // Time tracking
    float m_currentHour{12.0f};       // Current hour (0-23.999)
    int m_currentDay{1};              // Current day (starts at 1)
    float m_totalGameSeconds{0.0f};   // Total game seconds elapsed

    // Time progression
    float m_timeScale{1.0f};          // Scale factor for time progression

    // Daylight settings
    float m_sunriseHour{6.0f};        // Hour when sun rises
    float m_sunsetHour{18.0f};        // Hour when sun sets

    // Real-time tracking
    std::chrono::steady_clock::time_point m_lastUpdateTime;

    // Helper methods
    void advanceTime(float deltaGameSeconds);
};

#endif // GAME_TIME_HPP
