/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef DAY_NIGHT_CONTROLLER_HPP
#define DAY_NIGHT_CONTROLLER_HPP

/**
 * @file DayNightController.hpp
 * @brief Controller that tracks time periods and dispatches TimePeriodChangedEvent
 *
 * Subscribes to HourChangedEvent and dispatches TimePeriodChangedEvent when the
 * time period changes (Morning/Day/Evening/Night). Does NOT render - rendering
 * is handled by subscribers to the TimePeriodChangedEvent.
 *
 * Event flow:
 *   GameTime::dispatchTimeEvents() -> HourChangedEvent (Deferred)
 *     -> DayNightController detects period change
 *     -> Dispatches TimePeriodChangedEvent with visual config
 *     -> GamePlayState (or other subscribers) handle rendering
 */

#include "managers/EventManager.hpp"
#include "events/TimeEvent.hpp"
#include <vector>

class DayNightController
{
public:
    /**
     * @brief Get the singleton instance of DayNightController
     * @return Reference to the DayNightController instance
     */
    static DayNightController& Instance();

    /**
     * @brief Subscribe to time events and start tracking time periods
     * @note Called when a world state enters, NOT in GameEngine::init()
     */
    void subscribe();

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
     * @brief Get the current time period
     * @return Current TimePeriod enum value
     */
    TimePeriod getCurrentPeriod() const { return m_currentPeriod; }

    /**
     * @brief Get the current time period as string (zero allocation)
     * @return Static string pointer: "Morning", "Day", "Evening", or "Night"
     */
    const char* getCurrentPeriodString() const;

    /**
     * @brief Get the visual configuration for the current period
     * @return TimePeriodVisuals with overlay color values
     */
    TimePeriodVisuals getCurrentVisuals() const;

private:
    // Singleton pattern
    DayNightController() = default;
    ~DayNightController() = default;
    DayNightController(const DayNightController&) = delete;
    DayNightController& operator=(const DayNightController&) = delete;

    /**
     * @brief Handler for time events
     * @param data Event data containing the time event
     */
    void onTimeEvent(const EventData& data);

    /**
     * @brief Transition to a new time period and dispatch event
     * @param newPeriod The new time period
     */
    void transitionToPeriod(TimePeriod newPeriod);

    /**
     * @brief Determine time period from hour
     * @param hour Current game hour (0-23.999)
     * @return Corresponding TimePeriod
     */
    static TimePeriod hourToTimePeriod(float hour);

    // Subscription state
    bool m_subscribed{false};
    std::vector<EventManager::HandlerToken> m_handlerTokens;

    // Current state
    TimePeriod m_currentPeriod{TimePeriod::Day};
    TimePeriod m_previousPeriod{TimePeriod::Day};
};

#endif // DAY_NIGHT_CONTROLLER_HPP
