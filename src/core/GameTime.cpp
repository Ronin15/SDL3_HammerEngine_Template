/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "core/GameTime.hpp"
#include "events/TimeEvent.hpp"
#include "managers/EventManager.hpp"
#include <cmath>
#include <cstdio>
#include <random>

// ============================================================================
// SeasonConfig Implementation
// ============================================================================

SeasonConfig SeasonConfig::getDefault(Season season)
{
    SeasonConfig config;

    switch (season)
    {
        case Season::Spring:
            config.sunriseHour = 6.0f;
            config.sunsetHour = 19.0f;
            config.minTemperature = 45.0f;
            config.maxTemperature = 70.0f;
            config.weatherProbs.clear = 0.35f;
            config.weatherProbs.cloudy = 0.25f;
            config.weatherProbs.rainy = 0.25f;
            config.weatherProbs.stormy = 0.05f;
            config.weatherProbs.foggy = 0.05f;
            config.weatherProbs.snowy = 0.00f;
            config.weatherProbs.windy = 0.05f;
            break;

        case Season::Summer:
            config.sunriseHour = 5.0f;
            config.sunsetHour = 21.0f;
            config.minTemperature = 70.0f;
            config.maxTemperature = 95.0f;
            config.weatherProbs.clear = 0.50f;
            config.weatherProbs.cloudy = 0.20f;
            config.weatherProbs.rainy = 0.15f;
            config.weatherProbs.stormy = 0.10f;
            config.weatherProbs.foggy = 0.00f;
            config.weatherProbs.snowy = 0.00f;
            config.weatherProbs.windy = 0.05f;
            break;

        case Season::Fall:
            config.sunriseHour = 6.5f;
            config.sunsetHour = 18.0f;
            config.minTemperature = 40.0f;
            config.maxTemperature = 65.0f;
            config.weatherProbs.clear = 0.30f;
            config.weatherProbs.cloudy = 0.30f;
            config.weatherProbs.rainy = 0.20f;
            config.weatherProbs.stormy = 0.05f;
            config.weatherProbs.foggy = 0.10f;
            config.weatherProbs.snowy = 0.00f;
            config.weatherProbs.windy = 0.05f;
            break;

        case Season::Winter:
            config.sunriseHour = 7.5f;
            config.sunsetHour = 17.0f;
            config.minTemperature = 20.0f;
            config.maxTemperature = 45.0f;
            config.weatherProbs.clear = 0.25f;
            config.weatherProbs.cloudy = 0.25f;
            config.weatherProbs.rainy = 0.10f;
            config.weatherProbs.stormy = 0.05f;
            config.weatherProbs.foggy = 0.05f;
            config.weatherProbs.snowy = 0.25f;
            config.weatherProbs.windy = 0.05f;
            break;
    }

    return config;
}

// ============================================================================
// CalendarConfig Implementation
// ============================================================================

CalendarConfig CalendarConfig::createDefault()
{
    CalendarConfig config;
    config.months = {
        {"Bloomtide", 30, Season::Spring},
        {"Sunpeak", 30, Season::Summer},
        {"Harvestmoon", 30, Season::Fall},
        {"Frosthold", 30, Season::Winter}
    };
    return config;
}

int CalendarConfig::getTotalDaysInYear() const
{
    int total = 0;
    for (const auto& month : months)
    {
        total += month.dayCount;
    }
    return total;
}

// ============================================================================
// GameTime Implementation
// ============================================================================

GameTime::GameTime() :
    m_currentHour(12.0f),
    m_currentDay(1),
    m_totalGameSeconds(43200.0f), // 12 hours in seconds
    m_timeScale(1.0f),
    m_sunriseHour(6.0f),
    m_sunsetHour(18.0f),
    m_lastUpdateTime(std::chrono::steady_clock::now()),
    m_calendarConfig(CalendarConfig::createDefault()),
    m_currentMonth(0),
    m_dayOfMonth(1),
    m_currentYear(1),
    m_currentSeason(Season::Spring),
    m_currentSeasonConfig(SeasonConfig::getDefault(Season::Spring)),
    m_previousHour(-1),
    m_previousDay(-1),
    m_previousMonth(-1),
    m_previousYear(-1),
    m_previousSeason(Season::Spring),
    m_weatherCheckInterval(4.0f),
    m_lastWeatherCheckHour(0.0f),
    m_autoWeatherEnabled(false)
{
    // Initialize calendar state from current day
    updateCalendarState();
}

bool GameTime::init(float startHour, float timeScale)
{
    // Validate input parameters
    if (startHour < 0.0f || startHour >= 24.0f)
    {
        return false;
    }

    if (timeScale <= 0.0f)
    {
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

    // Initialize calendar state
    m_currentMonth = 0;
    m_dayOfMonth = 1;
    m_currentYear = 1;
    updateCalendarState();

    // Initialize previous state for change detection
    m_previousHour = static_cast<int>(startHour);
    m_previousDay = 1;
    m_previousMonth = 0;
    m_previousYear = 1;
    m_previousSeason = m_currentSeason;

    // Initialize weather check hour
    m_lastWeatherCheckHour = startHour;

    return true;
}

void GameTime::update(float deltaTime)
{
    // Do nothing if paused
    if (m_isPaused)
    {
        return;
    }

    // Store previous state for change detection
    m_previousHour = static_cast<int>(m_currentHour);
    m_previousDay = m_currentDay;
    m_previousMonth = m_currentMonth;
    m_previousYear = m_currentYear;
    m_previousSeason = m_currentSeason;

    // Convert delta time to game seconds based on time scale
    float deltaGameSeconds = deltaTime * m_timeScale;

    // Advance game time
    advanceTime(deltaGameSeconds);

    // Update calendar (month, year, season) based on new day
    updateCalendarState();

    // Dispatch time change events
    dispatchTimeEvents();

    // Check for automatic weather updates
    checkWeatherUpdate();
}

void GameTime::pause()
{
    m_isPaused = true;
}

void GameTime::resume()
{
    m_isPaused = false;
    // Reset last update time to avoid time jump after resume
    m_lastUpdateTime = std::chrono::steady_clock::now();
}

void GameTime::advanceTime(float deltaGameSeconds)
{
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

void GameTime::updateCalendarState()
{
    if (m_calendarConfig.months.empty())
    {
        // Fallback: use simple season calculation
        int seasonIndex = ((m_currentDay - 1) / 30) % 4;
        m_currentSeason = static_cast<Season>(seasonIndex);
        m_currentSeasonConfig = SeasonConfig::getDefault(m_currentSeason);
        return;
    }

    int totalDaysInYear = m_calendarConfig.getTotalDaysInYear();
    if (totalDaysInYear <= 0)
    {
        return;
    }

    // Calculate year and day within year (0-based)
    int daysSinceStart = m_currentDay - 1;  // Convert to 0-based
    m_currentYear = (daysSinceStart / totalDaysInYear) + 1;
    int dayInYear = daysSinceStart % totalDaysInYear;

    // Find which month this day falls into
    int accumulatedDays = 0;
    for (size_t i = 0; i < m_calendarConfig.months.size(); ++i)
    {
        const auto& month = m_calendarConfig.months[i];
        if (dayInYear < accumulatedDays + month.dayCount)
        {
            m_currentMonth = static_cast<int>(i);
            m_dayOfMonth = dayInYear - accumulatedDays + 1;  // Convert to 1-based
            break;
        }
        accumulatedDays += month.dayCount;
    }

    // Update season from current month
    updateSeasonFromCalendar();
}

void GameTime::updateSeasonFromCalendar()
{
    if (m_calendarConfig.months.empty())
    {
        return;
    }

    // Get season from current month
    if (m_currentMonth >= 0 &&
        m_currentMonth < static_cast<int>(m_calendarConfig.months.size()))
    {
        Season newSeason = m_calendarConfig.months[m_currentMonth].season;
        if (newSeason != m_currentSeason)
        {
            m_currentSeason = newSeason;
            m_currentSeasonConfig = SeasonConfig::getDefault(m_currentSeason);

            // Update daylight hours based on season
            m_sunriseHour = m_currentSeasonConfig.sunriseHour;
            m_sunsetHour = m_currentSeasonConfig.sunsetHour;
        }
    }
}

void GameTime::dispatchTimeEvents()
{
    // Check for hour change
    int currentHourInt = static_cast<int>(m_currentHour);
    if (currentHourInt != m_previousHour && m_previousHour >= 0)
    {
        auto event = std::make_shared<HourChangedEvent>(currentHourInt, isNighttime());
        EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Deferred);
    }

    // Check for day change
    if (m_currentDay != m_previousDay && m_previousDay >= 0)
    {
        // Convert string_view to string for event storage
        auto event = std::make_shared<DayChangedEvent>(
            m_currentDay, m_dayOfMonth, m_currentMonth, std::string(getCurrentMonthName()));
        EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Deferred);
    }

    // Check for month change
    if (m_currentMonth != m_previousMonth && m_previousMonth >= 0)
    {
        // Convert string_view to string for event storage
        auto event = std::make_shared<MonthChangedEvent>(
            m_currentMonth, std::string(getCurrentMonthName()), m_currentSeason);
        EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Deferred);
    }

    // Check for season change
    if (m_currentSeason != m_previousSeason)
    {
        auto event = std::make_shared<SeasonChangedEvent>(
            m_currentSeason, m_previousSeason, getSeasonName());
        EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Deferred);
    }

    // Check for year change
    if (m_currentYear != m_previousYear && m_previousYear >= 0)
    {
        auto event = std::make_shared<YearChangedEvent>(m_currentYear);
        EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Deferred);
    }
}

void GameTime::checkWeatherUpdate()
{
    if (!m_autoWeatherEnabled)
    {
        return;
    }

    // Calculate hours since last weather check
    float hoursSinceCheck = m_currentHour - m_lastWeatherCheckHour;
    if (hoursSinceCheck < 0.0f)
    {
        hoursSinceCheck += 24.0f;  // Handle midnight wraparound
    }

    // Also account for day changes
    if (m_currentDay != m_previousDay)
    {
        hoursSinceCheck += 24.0f * (m_currentDay - m_previousDay - 1);
    }

    if (hoursSinceCheck >= m_weatherCheckInterval)
    {
        m_lastWeatherCheckHour = m_currentHour;

        // Roll for recommended weather and dispatch WeatherCheckEvent
        // Subscribers (e.g., WeatherController) listen for this and call
        // EventManager::changeWeather() to actually change the weather
        WeatherType newWeather = rollWeatherForSeason();
        auto event = std::make_shared<WeatherCheckEvent>(m_currentSeason, newWeather);
        EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Deferred);
    }
}

void GameTime::setGameHour(float hour)
{
    // Validate and set hour
    if (hour >= 0.0f && hour < 24.0f)
    {
        float oldHour = m_currentHour;
        m_currentHour = hour;

        // Adjust total game seconds to match new hour
        float hourDiff = hour - oldHour;
        if (hourDiff < 0.0f)
        {
            hourDiff += 24.0f; // Handle wraparound (e.g., 23:00 to 01:00)
        }

        m_totalGameSeconds += hourDiff * 3600.0f;
    }
}

void GameTime::setDaylightHours(float sunrise, float sunset)
{
    // Validate input
    if (sunrise >= 0.0f && sunrise < 24.0f &&
        sunset >= 0.0f && sunset < 24.0f &&
        sunrise != sunset)
    {
        m_sunriseHour = sunrise;
        m_sunsetHour = sunset;
    }
}

bool GameTime::isDaytime() const
{
    if (m_sunriseHour < m_sunsetHour)
    {
        // Simple case: sunrise is before sunset
        return m_currentHour >= m_sunriseHour && m_currentHour < m_sunsetHour;
    }
    else
    {
        // Complex case: sunrise is after sunset (e.g., sunrise at 22:00, sunset at 4:00)
        return m_currentHour >= m_sunriseHour || m_currentHour < m_sunsetHour;
    }
}

bool GameTime::isNighttime() const
{
    return !isDaytime();
}

const char* GameTime::getTimeOfDayName() const
{
    if (m_currentHour >= 5.0f && m_currentHour < 8.0f)
        return "Morning";
    else if (m_currentHour >= 8.0f && m_currentHour < 17.0f)
        return "Day";
    else if (m_currentHour >= 17.0f && m_currentHour < 21.0f)
        return "Evening";
    else
        return "Night";
}

int GameTime::getCurrentSeason(int daysPerSeason) const
{
    // Validate input
    if (daysPerSeason <= 0)
    {
        daysPerSeason = 30; // Default fallback
    }

    // Calculate which season we're in based on current day
    // Seasons: 0=Spring, 1=Summer, 2=Fall, 3=Winter
    int seasonIndex = ((m_currentDay - 1) / daysPerSeason) % 4;
    return seasonIndex;
}

std::string_view GameTime::formatCurrentTime(bool use24Hour)
{
    int hours = static_cast<int>(m_currentHour);
    int minutes = static_cast<int>((m_currentHour - hours) * 60.0f);

    if (use24Hour)
    {
        // 24-hour format (e.g., "14:30")
        snprintf(m_timeFormatBuffer, sizeof(m_timeFormatBuffer),
                 "%02d:%02d", hours, minutes);
    }
    else
    {
        // 12-hour format (e.g., "2:30 PM")
        int displayHour = hours % 12;
        if (displayHour == 0)
            displayHour = 12;

        snprintf(m_timeFormatBuffer, sizeof(m_timeFormatBuffer),
                 "%d:%02d %s", displayHour, minutes, hours >= 12 ? "PM" : "AM");
    }

    return m_timeFormatBuffer;
}

// ============================================================================
// Calendar Methods
// ============================================================================

void GameTime::setCalendarConfig(const CalendarConfig& config)
{
    m_calendarConfig = config;
    updateCalendarState();
}

std::string_view GameTime::getCurrentMonthName() const
{
    if (m_calendarConfig.months.empty())
        return "Unknown";

    if (m_currentMonth >= 0 &&
        m_currentMonth < static_cast<int>(m_calendarConfig.months.size()))
    {
        return m_calendarConfig.months[m_currentMonth].name;
    }

    return "Unknown";
}

int GameTime::getDaysInCurrentMonth() const
{
    if (m_calendarConfig.months.empty())
    {
        return 30;  // Default
    }

    if (m_currentMonth >= 0 &&
        m_currentMonth < static_cast<int>(m_calendarConfig.months.size()))
    {
        return m_calendarConfig.months[m_currentMonth].dayCount;
    }

    return 30;
}

// ============================================================================
// Season Methods
// ============================================================================

const char* GameTime::getSeasonName() const
{
    switch (m_currentSeason)
    {
        case Season::Spring: return "Spring";
        case Season::Summer: return "Summer";
        case Season::Fall:   return "Fall";
        case Season::Winter: return "Winter";
        default:             return "Unknown";
    }
}

const SeasonConfig& GameTime::getSeasonConfig() const
{
    return m_currentSeasonConfig;
}

float GameTime::getCurrentTemperature() const
{
    const auto& config = m_currentSeasonConfig;

    // Temperature varies throughout the day
    // Coldest at 4 AM, warmest at 2 PM (14:00)
    // Create a temperature curve: min at 4AM, max at 2PM
    constexpr float coldestHour = 4.0f;

    float tempRange = config.maxTemperature - config.minTemperature;
    float hoursSinceColdest = m_currentHour - coldestHour;
    if (hoursSinceColdest < 0.0f)
    {
        hoursSinceColdest += 24.0f;
    }

    // Use cosine for smooth temperature curve
    float phase = (hoursSinceColdest / 24.0f) * 2.0f * 3.14159f;
    float tempFactor = (1.0f - std::cos(phase)) / 2.0f;

    return config.minTemperature + (tempRange * tempFactor);
}

// ============================================================================
// Weather Methods
// ============================================================================

void GameTime::setWeatherCheckInterval(float gameHours)
{
    if (gameHours > 0.0f)
    {
        m_weatherCheckInterval = gameHours;
    }
}

WeatherType GameTime::rollWeatherForSeason() const
{
    return rollWeatherForSeason(m_currentSeason);
}

WeatherType GameTime::rollWeatherForSeason(Season season) const
{
    // Get weather probabilities for the season
    SeasonConfig config = SeasonConfig::getDefault(season);
    const auto& probs = config.weatherProbs;

    // Generate random number
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float roll = dist(gen);

    // Accumulate probabilities and pick weather type
    float accumulated = 0.0f;

    accumulated += probs.clear;
    if (roll < accumulated) return WeatherType::Clear;

    accumulated += probs.cloudy;
    if (roll < accumulated) return WeatherType::Cloudy;

    accumulated += probs.rainy;
    if (roll < accumulated) return WeatherType::Rainy;

    accumulated += probs.stormy;
    if (roll < accumulated) return WeatherType::Stormy;

    accumulated += probs.foggy;
    if (roll < accumulated) return WeatherType::Foggy;

    accumulated += probs.snowy;
    if (roll < accumulated) return WeatherType::Snowy;

    accumulated += probs.windy;
    if (roll < accumulated) return WeatherType::Windy;

    // Default to Clear if probabilities don't sum to 1.0
    return WeatherType::Clear;
}
