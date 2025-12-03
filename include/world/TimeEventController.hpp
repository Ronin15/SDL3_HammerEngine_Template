/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef TIME_EVENT_CONTROLLER_HPP
#define TIME_EVENT_CONTROLLER_HPP

/**
 * @file TimeEventController.hpp
 * @brief Lightweight controller that logs GameTime events to the UI event log
 *
 * Subscribes to TimeEvents and formats user-friendly messages for:
 * - Hour changes (day/night transitions)
 * - Day changes
 * - Month changes
 * - Season changes
 * - Year changes
 * - Weather changes (from WeatherCheckEvent)
 *
 * Event flow:
 *   GameTime::dispatchTimeEvents() → TimeEvents (Deferred)
 *     → TimeEventController handles them
 *     → UIManager::addEventLogEntry() to display
 */

#include "managers/EventManager.hpp"
#include <string>
#include <string_view>
#include <vector>

class TimeEventController {
public:
    /**
     * @brief Get the singleton instance of TimeEventController
     * @return Reference to the TimeEventController instance
     */
    static TimeEventController& Instance();

    /**
     * @brief Subscribe to time events and set target event log
     * @param eventLogId ID of the UIManager event log component to write to
     * @note Called when a world state enters, NOT in GameEngine::init()
     */
    void subscribe(const std::string& eventLogId);

    /**
     * @brief Unsubscribe from time events
     * @note Called when a world state exits
     */
    void unsubscribe();

    /**
     * @brief Check if currently subscribed to time events
     * @return True if subscribed, false otherwise
     */
    bool isSubscribed() const { return m_subscribed; }

    /**
     * @brief Set the status label to update with time info
     * @param labelId ID of the UIManager label to update
     * @note Updates are event-driven, not per-frame
     */
    void setStatusLabel(std::string_view labelId);

private:
    // Singleton pattern
    TimeEventController() = default;
    ~TimeEventController() = default;
    TimeEventController(const TimeEventController&) = delete;
    TimeEventController& operator=(const TimeEventController&) = delete;

    /**
     * @brief Handler for time events - formats and logs messages
     * @param data Event data containing the time event
     */
    void onTimeEvent(const EventData& data);

    /**
     * @brief Update the status label with current time info
     * @note Called internally when time events fire
     */
    void updateStatusText();

    bool m_subscribed{false};
    std::string m_eventLogId;
    std::vector<EventManager::HandlerToken> m_handlerTokens;

    // Track previous state for day/night transition detection
    int m_previousHour{-1};
    bool m_wasNight{false};

    // Status label for time display
    std::string m_statusLabelId;
    char m_statusBuffer[80]{};
};

#endif // TIME_EVENT_CONTROLLER_HPP
