/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/world/WeatherController.hpp"
#include "events/TimeEvent.hpp"
#include "events/WeatherEvent.hpp"
#include "core/Logger.hpp"

void WeatherController::subscribe()
{
    if (checkAlreadySubscribed()) {
        return;
    }

    auto& eventMgr = EventManager::Instance();

    // Subscribe to Time events to handle WeatherCheckEvent
    auto token = eventMgr.registerHandlerWithToken(
        EventTypeId::Time,
        [this](const EventData& data) { onTimeEvent(data); }
    );
    addHandlerToken(token);

    setSubscribed(true);
    WEATHER_INFO("Subscribed to time events for automatic weather");
}

void WeatherController::onTimeEvent(const EventData& data)
{
    if (!data.event) {
        return;
    }

    // Check if this is a WeatherCheckEvent
    auto weatherCheck = std::dynamic_pointer_cast<WeatherCheckEvent>(data.event);
    if (!weatherCheck) {
        return;  // Not a weather check event, ignore
    }

    // Get recommended weather from the event (based on season probabilities)
    WeatherType recommended = weatherCheck->getRecommendedWeather();

    // Skip if weather hasn't changed - avoid duplicate event log messages
    if (recommended == m_currentWeather) {
        return;
    }

    // Track current weather for status bar display
    m_currentWeather = recommended;

    // Convert WeatherType enum to string for EventManager
    const char* weatherName =
        (recommended == WeatherType::Clear)  ? "Clear" :
        (recommended == WeatherType::Cloudy) ? "Cloudy" :
        (recommended == WeatherType::Rainy)  ? "Rainy" :
        (recommended == WeatherType::Stormy) ? "Stormy" :
        (recommended == WeatherType::Foggy)  ? "Foggy" :
        (recommended == WeatherType::Snowy)  ? "Snowy" :
        (recommended == WeatherType::Windy)  ? "Windy" : "Clear";

    // Trigger weather change (non-blocking, deferred dispatch)
    // This fires a WeatherEvent that ParticleManager subscribes to
    EventManager::Instance().changeWeather(weatherName, 2.0f,
        EventManager::DispatchMode::Deferred);

    WEATHER_DEBUG(weatherName);
}

const char* WeatherController::getCurrentWeatherString() const
{
    switch (m_currentWeather) {
        case WeatherType::Clear:  return "Clear";
        case WeatherType::Cloudy: return "Cloudy";
        case WeatherType::Rainy:  return "Rainy";
        case WeatherType::Stormy: return "Stormy";
        case WeatherType::Foggy:  return "Foggy";
        case WeatherType::Snowy:  return "Snowy";
        case WeatherType::Windy:  return "Windy";
        default: return "Clear";
    }
}
