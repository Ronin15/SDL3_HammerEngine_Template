/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "world/TimeEventController.hpp"
#include "core/GameTime.hpp"
#include "events/TimeEvent.hpp"
#include "events/WeatherEvent.hpp"
#include "managers/UIManager.hpp"
#include "world/WeatherController.hpp"
#include "core/Logger.hpp"
#include <cstdio>

TimeEventController& TimeEventController::Instance() {
    static TimeEventController instance;
    return instance;
}

void TimeEventController::subscribe(const std::string& eventLogId) {
    if (m_subscribed) {
        return;
    }

    m_eventLogId = eventLogId;
    auto& eventMgr = EventManager::Instance();

    // Subscribe to Time events to log them
    auto timeToken = eventMgr.registerHandlerWithToken(
        EventTypeId::Time,
        [this](const EventData& data) { onTimeEvent(data); }
    );
    m_handlerTokens.push_back(timeToken);

    // Subscribe to Weather events (actual weather changes, not checks)
    auto weatherToken = eventMgr.registerHandlerWithToken(
        EventTypeId::Weather,
        [this](const EventData& data) { onWeatherEvent(data); }
    );
    m_handlerTokens.push_back(weatherToken);

    m_subscribed = true;
    HAMMER_INFO("TimeEventController", "Subscribed to time and weather events");
}

void TimeEventController::unsubscribe() {
    if (!m_subscribed) {
        return;
    }

    auto& eventMgr = EventManager::Instance();
    for (const auto& token : m_handlerTokens) {
        eventMgr.removeHandler(token);
    }
    m_handlerTokens.clear();

    m_subscribed = false;
    m_previousHour = -1;
    m_wasNight = false;
    m_statusLabelId.clear();
    m_formatMode = StatusFormatMode::Default;
    HAMMER_INFO("TimeEventController", "Unsubscribed from time events");
}

void TimeEventController::setStatusLabel(std::string_view labelId) {
    m_statusLabelId = labelId;
    if (!labelId.empty()) {
        updateStatusText();  // Initial update
    }
}

void TimeEventController::setStatusFormatMode(StatusFormatMode mode) {
    m_formatMode = mode;
    if (!m_statusLabelId.empty()) {
        updateStatusText();  // Update with new format
    }
}

void TimeEventController::updateStatusText() {
    if (m_statusLabelId.empty()) {
        return;
    }

    auto& gt = GameTime::Instance();
    // All getters return const char* or string_view - zero heap allocations
    auto monthName = gt.getCurrentMonthName();
    auto timeStr = gt.formatCurrentTime();

    if (m_formatMode == StatusFormatMode::Extended) {
        // Extended format: Day X Month, Year Y | HH:MM TimeOfDay | Season | TempF | Weather
        auto& wc = WeatherController::Instance();
        snprintf(m_statusBuffer, sizeof(m_statusBuffer),
                 "Day %d %.*s, Year %d | %.*s %s | %s | %dF | %s",
                 gt.getDayOfMonth(),
                 static_cast<int>(monthName.size()), monthName.data(),
                 gt.getGameYear(),
                 static_cast<int>(timeStr.size()), timeStr.data(),
                 gt.getTimeOfDayName(),
                 gt.getSeasonName(),
                 static_cast<int>(gt.getCurrentTemperature()),
                 wc.getCurrentWeatherString());
    } else {
        // Default format: Day X Month, Year Y | HH:MM | TimeOfDay
        snprintf(m_statusBuffer, sizeof(m_statusBuffer),
                 "Day %d %.*s, Year %d | %.*s | %s",
                 gt.getDayOfMonth(),
                 static_cast<int>(monthName.size()), monthName.data(),
                 gt.getGameYear(),
                 static_cast<int>(timeStr.size()), timeStr.data(),
                 gt.getTimeOfDayName());
    }

    UIManager::Instance().setText(m_statusLabelId, m_statusBuffer);
}

void TimeEventController::onTimeEvent(const EventData& data) {
    if (!data.event) {
        return;
    }

    // Use TimeEventType enum to avoid expensive dynamic_cast chain
    auto timeEvent = std::static_pointer_cast<TimeEvent>(data.event);
    TimeEventType eventType = timeEvent->getTimeEventType();

    auto& ui = UIManager::Instance();
    bool hasEventLog = !m_eventLogId.empty();

    switch (eventType) {
        case TimeEventType::HourChanged: {
            // Most frequent event - only log day/night transitions
            auto hourEvent = std::static_pointer_cast<HourChangedEvent>(data.event);
            bool isNight = hourEvent->isNight();
            int hour = hourEvent->getHour();

            // Only log on actual day/night transition
            if (hasEventLog && m_previousHour >= 0 && isNight != m_wasNight) {
                ui.addEventLogEntry(m_eventLogId, isNight ? "Night falls" : "Dawn breaks");
            }

            m_previousHour = hour;
            m_wasNight = isNight;
            updateStatusText();
            break;
        }

        case TimeEventType::DayChanged: {
            if (hasEventLog) {
                auto dayEvent = std::static_pointer_cast<DayChangedEvent>(data.event);
                char buffer[64];
                snprintf(buffer, sizeof(buffer), "Day %d of %s",
                         dayEvent->getDayOfMonth(), dayEvent->getMonthName().c_str());
                ui.addEventLogEntry(m_eventLogId, buffer);
            }
            updateStatusText();
            break;
        }

        case TimeEventType::MonthChanged: {
            if (hasEventLog) {
                auto monthEvent = std::static_pointer_cast<MonthChangedEvent>(data.event);
                char buffer[64];
                snprintf(buffer, sizeof(buffer), "Month: %s", monthEvent->getMonthName().c_str());
                ui.addEventLogEntry(m_eventLogId, buffer);
            }
            updateStatusText();
            break;
        }

        case TimeEventType::SeasonChanged: {
            if (hasEventLog) {
                auto seasonEvent = std::static_pointer_cast<SeasonChangedEvent>(data.event);
                char buffer[64];
                snprintf(buffer, sizeof(buffer), "%s arrives", seasonEvent->getSeasonName().c_str());
                ui.addEventLogEntry(m_eventLogId, buffer);
            }
            break;
        }

        case TimeEventType::YearChanged: {
            if (hasEventLog) {
                auto yearEvent = std::static_pointer_cast<YearChangedEvent>(data.event);
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "Year %d", yearEvent->getYear());
                ui.addEventLogEntry(m_eventLogId, buffer);
            }
            updateStatusText();
            break;
        }

        case TimeEventType::WeatherCheck:
            // Weather logging handled by onWeatherEvent() which subscribes to
            // WeatherEvent (actual changes) instead of WeatherCheckEvent (periodic checks)
            updateStatusText();
            break;
    }
}

void TimeEventController::onWeatherEvent(const EventData& data) {
    if (!data.event) {
        return;
    }

    auto weatherEvent = std::dynamic_pointer_cast<WeatherEvent>(data.event);
    if (!weatherEvent) {
        return;
    }

    // Only log if we have an event log configured
    if (m_eventLogId.empty()) {
        return;
    }

    auto& ui = UIManager::Instance();
    std::string weatherStr = weatherEvent->getWeatherTypeString();

    // Map weather type to narrative message
    const char* weatherName =
        (weatherStr == "Clear")  ? "Clear skies" :
        (weatherStr == "Cloudy") ? "Clouds gather" :
        (weatherStr == "Rainy")  ? "Rain begins" :
        (weatherStr == "Stormy") ? "Storm approaches" :
        (weatherStr == "Foggy")  ? "Fog rolls in" :
        (weatherStr == "Snowy")  ? "Snow falls" :
        (weatherStr == "Windy")  ? "Wind picks up" : "Weather changes";

    ui.addEventLogEntry(m_eventLogId, weatherName);
    updateStatusText();
}
