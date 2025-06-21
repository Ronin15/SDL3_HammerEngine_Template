/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "core/GameTime.hpp"
#include <iomanip>
#include <sstream>
#include <cmath>

GameTime::GameTime() :
    m_currentHour(12.0f),
    m_currentDay(1),
    m_totalGameSeconds(43200.0f), // 12 hours in seconds
    m_timeScale(1.0f),
    m_sunriseHour(6.0f),
    m_sunsetHour(18.0f),
    m_lastUpdateTime(std::chrono::steady_clock::now()) {
}

bool GameTime::init(float startHour, float timeScale) {
    // Validate input parameters
    if (startHour < 0.0f || startHour >= 24.0f) {
        return false;
    }

    if (timeScale <= 0.0f) {
        return false;
    }

    // Set initial time values
    m_currentHour = startHour;
    m_currentDay = 1;
    m_timeScale = timeScale;

    // Calculate total seconds based on current hour
    m_totalGameSeconds = startHour * 3600.0f;

    // Initialize last update time
    m_lastUpdateTime = std::chrono::steady_clock::now();

    return true;
}

void GameTime::update(float deltaTimeMs) {
    // Convert delta time to game seconds based on time scale
    float deltaGameSeconds = (deltaTimeMs / 1000.0f) * m_timeScale;

    // Advance game time
    advanceTime(deltaGameSeconds);
}

void GameTime::advanceTime(float deltaGameSeconds) {
    // Increment total game seconds
    m_totalGameSeconds += deltaGameSeconds;

    // Calculate new hour and day
    float totalHours = m_totalGameSeconds / 3600.0f;
    float newHour = std::fmod(totalHours, 24.0f);
    int newDay = static_cast<int>(totalHours / 24.0f) + 1;

    // Update hour and day
    m_currentHour = newHour;
    m_currentDay = newDay;
}

void GameTime::setGameHour(float hour) {
    // Validate and set hour
    if (hour >= 0.0f && hour < 24.0f) {
        float oldHour = m_currentHour;
        m_currentHour = hour;

        // Adjust total game seconds to match new hour
        float hourDiff = hour - oldHour;
        if (hourDiff < 0.0f) {
            hourDiff += 24.0f; // Handle wraparound (e.g., 23:00 to 01:00)
        }

        m_totalGameSeconds += hourDiff * 3600.0f;
    }
}

void GameTime::setDaylightHours(float sunrise, float sunset) {
    // Validate input
    if (sunrise >= 0.0f && sunrise < 24.0f &&
        sunset >= 0.0f && sunset < 24.0f &&
        sunrise != sunset) {

        m_sunriseHour = sunrise;
        m_sunsetHour = sunset;
    }
}

bool GameTime::isDaytime() const {
    if (m_sunriseHour < m_sunsetHour) {
        // Simple case: sunrise is before sunset
        return m_currentHour >= m_sunriseHour && m_currentHour < m_sunsetHour;
    } else {
        // Complex case: sunrise is after sunset (e.g., sunrise at 22:00, sunset at 4:00)
        return m_currentHour >= m_sunriseHour || m_currentHour < m_sunsetHour;
    }
}

bool GameTime::isNighttime() const {
    return !isDaytime();
}

std::string GameTime::getTimeOfDayName() const {
    if (m_currentHour >= 5.0f && m_currentHour < 8.0f) {
        return "Morning";
    } else if (m_currentHour >= 8.0f && m_currentHour < 17.0f) {
        return "Day";
    } else if (m_currentHour >= 17.0f && m_currentHour < 21.0f) {
        return "Evening";
    } else {
        return "Night";
    }
}

int GameTime::getCurrentSeason(int daysPerSeason) const {
    // Validate input
    if (daysPerSeason <= 0) {
        daysPerSeason = 30; // Default fallback
    }

    // Calculate which season we're in based on current day
    // Seasons: 0=Spring, 1=Summer, 2=Fall, 3=Winter
    int seasonIndex = ((m_currentDay - 1) / daysPerSeason) % 4;
    return seasonIndex;
}

std::string GameTime::formatCurrentTime(bool use24Hour) const {
    int hours = static_cast<int>(m_currentHour);
    int minutes = static_cast<int>((m_currentHour - hours) * 60.0f);

    std::stringstream ss;

    if (use24Hour) {
        // 24-hour format (e.g., "14:30")
        ss << std::setw(2) << std::setfill('0') << hours << ":"
           << std::setw(2) << std::setfill('0') << minutes;
    } else {
        // 12-hour format (e.g., "2:30 PM")
        int displayHour = hours % 12;
        if (displayHour == 0) {
            displayHour = 12; // 0 should display as 12 in 12-hour format
        }

        ss << displayHour << ":"
           << std::setw(2) << std::setfill('0') << minutes
           << (hours >= 12 ? " PM" : " AM");
    }

    return ss.str();
}
