/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef WEATHER_CONTROLLER_HPP
#define WEATHER_CONTROLLER_HPP

/**
 * @file WeatherController.hpp
 * @brief Lightweight controller that bridges GameTime weather checks to actual weather changes
 *
 * WeatherController subscribes to WeatherCheckEvent (from GameTime) and triggers actual
 * weather changes via EventManager::changeWeather(). This is a controller, not a manager -
 * it's an event subscriber that reacts to time events, not a system initialized in GameEngine.
 *
 * Event flow:
 *   GameTime::checkWeatherUpdate() → WeatherCheckEvent (Deferred)
 *     → WeatherController handles it
 *     → EventManager::changeWeather() (Deferred)
 *     → WeatherEvent dispatched
 *     → ParticleManager handles it → Visual effects rendered
 */

#include "managers/EventManager.hpp"
#include <vector>

// Forward declaration
enum class WeatherType;

class WeatherController {
public:
    /**
     * @brief Get the singleton instance of WeatherController
     * @return Reference to the WeatherController instance
     */
    static WeatherController& Instance();

    /**
     * @brief Subscribe to weather check events
     * @note Called when a world state enters, NOT in GameEngine::init()
     */
    void subscribe();

    /**
     * @brief Unsubscribe from weather check events
     * @note Called when a world state exits
     */
    void unsubscribe();

    /**
     * @brief Check if currently subscribed to weather events
     * @return True if subscribed, false otherwise
     */
    bool isSubscribed() const { return m_subscribed; }

    /**
     * @brief Get the current weather type
     * @return Current WeatherType enum value (defaults to Clear)
     */
    WeatherType getCurrentWeather() const { return m_currentWeather; }

    /**
     * @brief Get current weather as string (zero allocation)
     * @return Static string pointer: "Clear", "Rainy", etc.
     */
    const char* getCurrentWeatherString() const;

private:
    // Singleton pattern
    WeatherController() = default;
    ~WeatherController() = default;
    WeatherController(const WeatherController&) = delete;
    WeatherController& operator=(const WeatherController&) = delete;

    /**
     * @brief Handler for time events - filters for WeatherCheckEvent
     * @param data Event data containing the time event
     */
    void onTimeEvent(const EventData& data);

    bool m_subscribed{false};
    std::vector<EventManager::HandlerToken> m_handlerTokens;
    WeatherType m_currentWeather{};  // Initialized in cpp to avoid header dependency
};

#endif // WEATHER_CONTROLLER_HPP
