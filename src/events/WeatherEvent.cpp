/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "events/WeatherEvent.hpp"
#include "utils/Vector2D.hpp"
#include <iostream>
#include <random>
#include <chrono>
#include <algorithm>

// Helper for getting current game time (hour of day)
static float getCurrentGameTime() {
    // This would typically come from a game time system
    // For now, return a placeholder value between 0-24
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    const std::tm* localTime = std::localtime(&time);

    return static_cast<float>(localTime->tm_hour) +
           static_cast<float>(localTime->tm_min) / 60.0f;
}

// Helper for getting current player position
static Vector2D getPlayerPosition() {
    // This would typically come from the player entity
    // For now, return a placeholder position
    return Vector2D(0.0f, 0.0f);
}

// Helper for getting current season
static int getCurrentSeason() {
    // This would typically come from a game calendar system
    // For now, determine season based on real-world month
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    const std::tm* localTime = std::localtime(&time);

    int month = localTime->tm_mon; // 0-11

    // 0=spring, 1=summer, 2=fall, 3=winter
    if (month >= 2 && month <= 4) return 0; // Spring (Mar-May)
    if (month >= 5 && month <= 7) return 1; // Summer (Jun-Aug)
    if (month >= 8 && month <= 10) return 2; // Fall (Sep-Nov)
    return 3; // Winter (Dec-Feb)
}

WeatherEvent::WeatherEvent(const std::string& name, WeatherType type)
    : m_name(name), m_weatherType(type) {

    // Set default parameters based on weather type
    switch (type) {
        case WeatherType::Clear:
            m_params.intensity = 0.0f;
            m_params.visibility = 1.0f;
            m_params.windSpeed = 0.1f;
            break;
        case WeatherType::Cloudy:
            m_params.intensity = 0.5f;
            m_params.visibility = 0.8f;
            m_params.windSpeed = 0.3f;
            break;
        case WeatherType::Rainy:
            m_params.intensity = 0.7f;
            m_params.visibility = 0.6f;
            m_params.windSpeed = 0.5f;
            m_params.particleEffect = "rain";
            m_params.soundEffect = "rain_ambient";
            break;
        case WeatherType::Stormy:
            m_params.intensity = 1.0f;
            m_params.visibility = 0.3f;
            m_params.windSpeed = 0.9f;
            m_params.particleEffect = "heavy_rain";
            m_params.soundEffect = "thunder_storm";
            break;
        case WeatherType::Foggy:
            m_params.intensity = 0.6f;
            m_params.visibility = 0.2f;
            m_params.windSpeed = 0.1f;
            m_params.particleEffect = "fog";
            break;
        case WeatherType::Snowy:
            m_params.intensity = 0.7f;
            m_params.visibility = 0.5f;
            m_params.windSpeed = 0.4f;
            m_params.particleEffect = "snow";
            m_params.soundEffect = "snow_ambient";
            break;
        case WeatherType::Windy:
            m_params.intensity = 0.6f;
            m_params.visibility = 0.9f;
            m_params.windSpeed = 1.0f;
            m_params.soundEffect = "wind_ambient";
            break;
        default:
            break;
    }
}

WeatherEvent::WeatherEvent(const std::string& name, const std::string& customType)
    : m_name(name), m_weatherType(WeatherType::Custom), m_customType(customType) {

    // Default parameters for custom weather
    m_params.intensity = 0.5f;
    m_params.visibility = 0.8f;
    m_params.windSpeed = 0.3f;
}

void WeatherEvent::update() {
    // Skip update if not active or on cooldown
    if (!m_active || m_onCooldown) {
        return;
    }

    // Update transition if in progress
    if (m_inTransition) {
        // Transition logic would be implemented here
        // This would gradually blend between weather states
    }

    // Update frame counter for frequency control
    m_frameCounter++;
    if (m_updateFrequency > 1 && m_frameCounter % m_updateFrequency != 0) {
        return;
    }

    // Reset frame counter to prevent overflow
    if (m_frameCounter >= 10000) {
        m_frameCounter = 0;
    }
}

void WeatherEvent::execute() {
    // Mark as triggered
    m_hasTriggered = true;

    // Start cooldown if set
    if (m_cooldownTime > 0.0f) {
        m_onCooldown = true;
        m_cooldownTimer = 0.0f;
    }

    // Begin transition to this weather
    m_inTransition = true;
    m_transitionProgress = 0.0f;

    // Apply this weather type to the game world
    // In a real implementation, this would interact with rendering systems
    std::cout << "Weather changing to: " << getWeatherTypeString()
              << " (Intensity: " << m_params.intensity
              << ", Visibility: " << m_params.visibility << ")" << std::endl;

    // Trigger particle effects if specified
    if (!m_params.particleEffect.empty()) {
        std::cout << "Starting particle effect: " << m_params.particleEffect << std::endl;
    }

    // Play sound effects if specified
    if (!m_params.soundEffect.empty()) {
        std::cout << "Playing sound effect: " << m_params.soundEffect << std::endl;
    }
}

void WeatherEvent::reset() {
    m_onCooldown = false;
    m_cooldownTimer = 0.0f;
    m_inTransition = false;
    m_transitionProgress = 0.0f;
    m_hasTriggered = false;
}

void WeatherEvent::clean() {
    // Clean up any resources specific to this weather event
    m_conditions.clear();

    // Reset time conditions
    m_startHour = -1.0f;
    m_endHour = -1.0f;
    m_season = -1;

    // Reset location conditions
    m_regionName.clear();
    m_useGeographicBounds = false;

    // Reset state
    m_inTransition = false;
    m_transitionProgress = 0.0f;
    m_hasTriggered = false;
}

std::string WeatherEvent::getWeatherTypeString() const {
    if (m_weatherType == WeatherType::Custom) {
        return m_customType;
    }

    switch (m_weatherType) {
        case WeatherType::Clear: return "Clear";
        case WeatherType::Cloudy: return "Cloudy";
        case WeatherType::Rainy: return "Rainy";
        case WeatherType::Stormy: return "Stormy";
        case WeatherType::Foggy: return "Foggy";
        case WeatherType::Snowy: return "Snowy";
        case WeatherType::Windy: return "Windy";
        default: return "Unknown";
    }
}

void WeatherEvent::setWeatherType(WeatherType type) {
    m_weatherType = type;
    m_customType.clear(); // Clear custom type when setting a standard type
}

void WeatherEvent::setWeatherType(const std::string& customType) {
    m_weatherType = WeatherType::Custom;
    m_customType = customType;
}

bool WeatherEvent::checkConditions() {
    // If there are no conditions at all, return false
    if (m_conditions.empty() && !m_useGeographicBounds && m_startHour < 0 && m_season < 0 && m_regionName.empty()) {
        return false;
    }

    // Check custom conditions first - if any fail, return false
    if (!std::all_of(m_conditions.begin(), m_conditions.end(), 
                     [](const auto& condition) { return condition(); })) {
        return false;
    }

    // If we only have custom conditions (no environmental ones),
    // and all have passed (we've reached this point), return true
    if (!m_conditions.empty() && !m_useGeographicBounds && m_startHour < 0 && m_season < 0 && m_regionName.empty()) {
        return true;
    }

    // Check time condition
    if (m_startHour >= 0 && !checkTimeCondition()) {
        return false;
    }

    // Check location condition
    if ((m_useGeographicBounds || !m_regionName.empty()) && !checkLocationCondition()) {
        return false;
    }

    // Check season if specified
    if (m_season >= 0 && m_season != getCurrentSeason()) {
        return false;
    }

    // All conditions passed
    return true;
}

void WeatherEvent::addTimeCondition(std::function<bool()> condition) {
    // Clear existing conditions first to make tests more predictable
    m_conditions.clear();
    // Add the new condition
    m_conditions.push_back(condition);
}

void WeatherEvent::addLocationCondition(std::function<bool()> condition) {
    m_conditions.push_back(condition);
}

void WeatherEvent::addRandomChanceCondition(float probability) {
    // Create a condition that returns true with the given probability
    m_conditions.push_back([probability]() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(0.0f, 1.0f);
        return dis(gen) < probability;
    });
}

void WeatherEvent::setTimeOfDay(float startHour, float endHour) {
    m_startHour = startHour;
    m_endHour = endHour;
}

void WeatherEvent::setSeasonalEffect(int season) {
    m_season = season;
}

void WeatherEvent::setGeographicRegion(const std::string& regionName) {
    m_regionName = regionName;
}

void WeatherEvent::setBoundingArea(float x1, float y1, float x2, float y2) {
    m_useGeographicBounds = true;
    m_boundX1 = x1;
    m_boundY1 = y1;
    m_boundX2 = x2;
    m_boundY2 = y2;
}

void WeatherEvent::forceWeatherChange(WeatherType type, float transitionTime) {
    // Static method that would interact with a central weather system
    std::cout << "Forcing weather change to: " << static_cast<int>(type)
              << " with transition time: " << transitionTime << "s" << std::endl;

    // This would typically call into a game system that manages weather
}

void WeatherEvent::forceWeatherChange(const std::string& customType, float transitionTime) {
    // Static method that would interact with a central weather system
    std::cout << "Forcing weather change to custom type: " << customType
              << " with transition time: " << transitionTime << "s" << std::endl;

    // This would typically call into a game system that manages weather
}

bool WeatherEvent::checkTimeCondition() const {
    if (m_startHour < 0 || m_endHour < 0) {
        return true; // No time restriction
    }

    float currentHour = getCurrentGameTime();

    if (m_startHour <= m_endHour) {
        // Simple case: start time is before end time
        return currentHour >= m_startHour && currentHour <= m_endHour;
    } else {
        // Wrapping case: start time is after end time (spans midnight)
        return currentHour >= m_startHour || currentHour <= m_endHour;
    }
}

bool WeatherEvent::checkLocationCondition() const {
    if (!m_useGeographicBounds && m_regionName.empty()) {
        return true; // No location restriction
    }

    // Check region first
    // TODO: Implement proper region checking when region system is available
    // Currently region checking is disabled (placeholder always returns true)
    // if (!m_regionName.empty() && !isInRegion()) {
    //     return false;
    // }

    // Then check bounding area
    if (m_useGeographicBounds && !isInBounds()) {
        return false;
    }

    return true;
}

bool WeatherEvent::isInRegion() const {
    // TODO: Implement proper region checking when region system is available
    // This should check the player's current region against m_regionName
    // For now, always return true to allow all weather events
    return true;
}

bool WeatherEvent::isInBounds() const {
    if (!m_useGeographicBounds) {
        return true; // No bounds restriction
    }

    Vector2D playerPos = getPlayerPosition();

    return playerPos.getX() >= m_boundX1 && playerPos.getX() <= m_boundX2 &&
           playerPos.getY() >= m_boundY1 && playerPos.getY() <= m_boundY2;
}
