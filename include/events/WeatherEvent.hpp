/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef WEATHER_EVENT_HPP
#define WEATHER_EVENT_HPP

/**
 * @file WeatherEvent.hpp
 * @brief Weather event implementation for dynamic weather changes
 *
 * WeatherEvent allows the game to trigger weather changes based on:
 * - Time of day
 * - Geographic location
 * - Seasonal changes
 * - Story progression
 * - Random chance
 */

#include "Event.hpp"
#include <string>
#include <vector>
#include <functional>
#include <iostream>

enum class WeatherType {
    Clear,
    Cloudy,
    Rainy,
    Stormy,
    Foggy,
    Snowy,
    Windy,
    Custom
};

// Stream operator for WeatherType enum to support Boost.Test output
inline std::ostream& operator<<(std::ostream& os, const WeatherType& type) {
    switch (type) {
        case WeatherType::Clear: os << "Clear"; break;
        case WeatherType::Cloudy: os << "Cloudy"; break;
        case WeatherType::Rainy: os << "Rainy"; break;
        case WeatherType::Stormy: os << "Stormy"; break;
        case WeatherType::Foggy: os << "Foggy"; break;
        case WeatherType::Snowy: os << "Snowy"; break;
        case WeatherType::Windy: os << "Windy"; break;
        case WeatherType::Custom: os << "Custom"; break;
        default: os << "Unknown"; break;
    }
    return os;
}

struct WeatherParams {
    float intensity{1.0f};       // 0.0 to 1.0 intensity level
    float windSpeed{0.0f};       // Wind speed in arbitrary units
    float windDirection{0.0f};   // Direction in degrees (0-359)
    float visibility{1.0f};      // 0.0 (no visibility) to 1.0 (full visibility)
    float transitionTime{5.0f};  // Time in seconds to transition to this weather
    std::string particleEffect{}; // Optional particle effect ID
    std::string soundEffect{};    // Optional sound effect ID

    // Color modifiers for environment rendering
    float colorR{1.0f};
    float colorG{1.0f};
    float colorB{1.0f};
    float colorA{1.0f};

    // Default constructor
    WeatherParams() = default;

    // Constructor with commonly used parameters
    WeatherParams(float intensity, float transition, float visibility = 1.0f)
        : intensity(intensity), visibility(visibility), transitionTime(transition) {}
};

class WeatherEvent : public Event {
public:
    WeatherEvent(const std::string& name, WeatherType type);
    WeatherEvent(const std::string& name, const std::string& customType);
    virtual ~WeatherEvent() override = default;

    // Core event methods implementation
    void update() override;
    void execute() override;
    void reset() override;
    void clean() override;

    // Event identification
    std::string getName() const override { return m_name; }
    std::string getType() const override { return "Weather"; }

    // Weather-specific methods
    WeatherType getWeatherType() const { return m_weatherType; }
    std::string getWeatherTypeString() const;
    void setWeatherType(WeatherType type);
    void setWeatherType(const std::string& customType);

    // Weather parameters
    const WeatherParams& getWeatherParams() const { return m_params; }
    void setWeatherParams(const WeatherParams& params) { m_params = params; }

    // Condition checking
    bool checkConditions() override;

    // Add condition functions
    void addTimeCondition(std::function<bool()> condition);
    void addLocationCondition(std::function<bool()> condition);
    void addRandomChanceCondition(float probability); // 0.0 to 1.0

    // Time-based control
    void setTimeOfDay(float startHour, float endHour);
    void setSeasonalEffect(int season); // 0=spring, 1=summer, 2=fall, 3=winter

    // Location-based control
    void setGeographicRegion(const std::string& regionName);
    void setBoundingArea(float x1, float y1, float x2, float y2);

    // Direct weather change access (for scripting)
    static void forceWeatherChange(WeatherType type, float transitionTime = 5.0f);
    static void forceWeatherChange(const std::string& customType, float transitionTime = 5.0f);

private:
    std::string m_name;
    WeatherType m_weatherType{WeatherType::Clear};
    std::string m_customType; // Used when type is Custom
    WeatherParams m_params;

    // Condition tracking
    std::vector<std::function<bool()>> m_conditions;

    // Time-based parameters
    float m_startHour{-1.0f}; // -1 means no time restriction
    float m_endHour{-1.0f};   // -1 means no time restriction
    int m_season{-1};         // -1 means all seasons

    // Geographic parameters
    std::string m_regionName;
    bool m_useGeographicBounds{false};
    float m_boundX1{0.0f}, m_boundY1{0.0f}, m_boundX2{0.0f}, m_boundY2{0.0f};

    // Internal state
    float m_transitionProgress{0.0f};
    bool m_inTransition{false};

    // Helper methods
    bool checkTimeCondition() const;
    bool checkLocationCondition() const;
    bool isInRegion() const;
    bool isInBounds() const;
};

#endif // WEATHER_EVENT_HPP
