/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/world/TimeController.hpp"
#include "core/GameTime.hpp"
#include "events/TimeEvent.hpp"
#include "events/WeatherEvent.hpp"
#include "managers/UIManager.hpp"
#include "controllers/world/WeatherController.hpp"
#include "core/Logger.hpp"
#include <format>

TimeController& TimeController::Instance() {
    static TimeController instance;
    return instance;
}

void TimeController::subscribe(const std::string& eventLogId) {
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
    HAMMER_INFO("TimeController", "Subscribed to time and weather events");
}

void TimeController::unsubscribe() {
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
    HAMMER_INFO("TimeController", "Unsubscribed from time events");
}

void TimeController::setStatusLabel(std::string_view labelId) {
    m_statusLabelId = labelId;
    if (!labelId.empty()) {
        m_statusBuffer.reserve(256);  // Pre-allocate for zero per-frame allocations
        updateStatusText();  // Initial update
    }
}

void TimeController::setStatusFormatMode(StatusFormatMode mode) {
    m_formatMode = mode;
    if (!m_statusLabelId.empty()) {
        updateStatusText();  // Update with new format
    }
}

void TimeController::updateStatusText() {
    if (m_statusLabelId.empty()) {
        return;
    }

    auto& gt = GameTime::Instance();
    // All getters return const char* or string_view - zero heap allocations
    auto monthName = gt.getCurrentMonthName();
    auto timeStr = gt.formatCurrentTime();

    m_statusBuffer.clear();  // Keeps reserved capacity
    if (m_formatMode == StatusFormatMode::Extended) {
        // Extended format: Day X Month, Year Y | HH:MM TimeOfDay | Season | TempF | Weather
        const auto& wc = WeatherController::Instance();
        std::format_to(std::back_inserter(m_statusBuffer),
                       "Day {} {}, Year {} | {} {} | {} | {}F | {}",
                       gt.getDayOfMonth(), monthName, gt.getGameYear(),
                       timeStr, gt.getTimeOfDayName(),
                       gt.getSeasonName(),
                       static_cast<int>(gt.getCurrentTemperature()),
                       wc.getCurrentWeatherString());
    } else {
        // Default format: Day X Month, Year Y | HH:MM | TimeOfDay
        std::format_to(std::back_inserter(m_statusBuffer),
                       "Day {} {}, Year {} | {} | {}",
                       gt.getDayOfMonth(), monthName, gt.getGameYear(),
                       timeStr, gt.getTimeOfDayName());
    }

    UIManager::Instance().setText(m_statusLabelId, m_statusBuffer);
}

void TimeController::onTimeEvent(const EventData& data) {
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
            // Update status text on every hour change
            auto hourEvent = std::static_pointer_cast<HourChangedEvent>(data.event);
            m_previousHour = hourEvent->getHour();
            m_wasNight = hourEvent->isNight();
            updateStatusText();
            break;
        }

        case TimeEventType::DayChanged: {
            if (hasEventLog) {
                auto dayEvent = std::static_pointer_cast<DayChangedEvent>(data.event);
                ui.addEventLogEntry(m_eventLogId,
                    std::format("Day {} of {}", dayEvent->getDayOfMonth(), dayEvent->getMonthName()));
            }
            updateStatusText();
            break;
        }

        case TimeEventType::MonthChanged: {
            if (hasEventLog) {
                auto monthEvent = std::static_pointer_cast<MonthChangedEvent>(data.event);
                ui.addEventLogEntry(m_eventLogId,
                    std::format("Month: {}", monthEvent->getMonthName()));
            }
            updateStatusText();
            break;
        }

        case TimeEventType::SeasonChanged: {
            if (hasEventLog) {
                auto seasonEvent = std::static_pointer_cast<SeasonChangedEvent>(data.event);
                ui.addEventLogEntry(m_eventLogId,
                    std::format("{} arrives", seasonEvent->getSeasonName()));
            }
            break;
        }

        case TimeEventType::YearChanged: {
            if (hasEventLog) {
                auto yearEvent = std::static_pointer_cast<YearChangedEvent>(data.event);
                ui.addEventLogEntry(m_eventLogId,
                    std::format("Year {}", yearEvent->getYear()));
            }
            updateStatusText();
            break;
        }

        case TimeEventType::WeatherCheck:
            // Weather logging handled by onWeatherEvent() which subscribes to
            // WeatherEvent (actual changes) instead of WeatherCheckEvent (periodic checks)
            updateStatusText();
            break;

        case TimeEventType::TimePeriodChanged: {
            // Log period-specific messages to event log
            if (hasEventLog) {
                auto periodEvent = std::static_pointer_cast<TimePeriodChangedEvent>(data.event);
                TimePeriod period = periodEvent->getPeriod();

                const char* message = nullptr;
                switch (period) {
                    case TimePeriod::Morning: message = "Dawn breaks"; break;
                    case TimePeriod::Day:     message = "The sun rises high"; break;
                    case TimePeriod::Evening: message = "Dusk settles in"; break;
                    case TimePeriod::Night:   message = "Night falls"; break;
                }
                if (message) {
                    ui.addEventLogEntry(m_eventLogId, message);
                }
            }
            break;
        }
    }
}

void TimeController::onWeatherEvent(const EventData& data) {
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

    // Map weather type to narrative message using enum (type-safe, zero allocation)
    std::string_view weatherName;
    switch (weatherEvent->getWeatherType()) {
        case WeatherType::Clear:  weatherName = "Clear skies"; break;
        case WeatherType::Cloudy: weatherName = "Clouds gather"; break;
        case WeatherType::Rainy:  weatherName = "Rain begins"; break;
        case WeatherType::Stormy: weatherName = "Storm approaches"; break;
        case WeatherType::Foggy:  weatherName = "Fog rolls in"; break;
        case WeatherType::Snowy:  weatherName = "Snow falls"; break;
        case WeatherType::Windy:  weatherName = "Wind picks up"; break;
        default:                  weatherName = "Weather changes"; break;
    }

    ui.addEventLogEntry(m_eventLogId, std::string(weatherName));
    updateStatusText();
}
