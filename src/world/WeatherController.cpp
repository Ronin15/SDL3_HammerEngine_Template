/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "world/WeatherController.hpp"
#include "events/TimeEvent.hpp"
#include "core/Logger.hpp"

WeatherController& WeatherController::Instance() {
    static WeatherController instance;
    return instance;
}

void WeatherController::subscribe() {
    if (m_subscribed) {
        return;
    }

    auto& eventMgr = EventManager::Instance();

    // Subscribe to Time events to handle WeatherCheckEvent
    auto token = eventMgr.registerHandlerWithToken(
        EventTypeId::Time,
        [this](const EventData& data) { onTimeEvent(data); }
    );
    m_handlerTokens.push_back(token);

    m_subscribed = true;
    HAMMER_INFO("WeatherController", "Subscribed to time events for automatic weather");
}

void WeatherController::unsubscribe() {
    if (!m_subscribed) {
        return;
    }

    auto& eventMgr = EventManager::Instance();
    for (const auto& token : m_handlerTokens) {
        eventMgr.removeHandler(token);
    }
    m_handlerTokens.clear();

    m_subscribed = false;
    HAMMER_INFO("WeatherController", "Unsubscribed from time events");
}

void WeatherController::onTimeEvent(const EventData& data) {
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

    HAMMER_DEBUG("WeatherController", "Season-based weather change to " + std::string(weatherName));
}
