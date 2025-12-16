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
 * Ownership: GameState owns the controller instance (not a singleton).
 *
 * Event flow:
 *   GameTime::dispatchTimeEvents() -> HourChangedEvent (Deferred)
 *     -> DayNightController detects period change
 *     -> Dispatches TimePeriodChangedEvent with visual config
 *     -> GamePlayState (or other subscribers) handle rendering
 */

#include "controllers/ControllerBase.hpp"
#include "events/TimeEvent.hpp"

class DayNightController : public ControllerBase
{
public:
    DayNightController() = default;
    ~DayNightController() override = default;

    // Movable (inherited from base)
    DayNightController(DayNightController&&) noexcept = default;
    DayNightController& operator=(DayNightController&&) noexcept = default;

    /**
     * @brief Subscribe to time events and start tracking time periods
     * @note Called when a world state enters, NOT in GameEngine::init()
     */
    void subscribe();

    /**
     * @brief Get the current time period
     * @return Current TimePeriod enum value
     */
    [[nodiscard]] TimePeriod getCurrentPeriod() const { return m_currentPeriod; }

    /**
     * @brief Get the current time period as string (zero allocation)
     * @return Static string pointer: "Morning", "Day", "Evening", or "Night"
     */
    [[nodiscard]] const char* getCurrentPeriodString() const;

    /**
     * @brief Get the visual configuration for the current period
     * @return TimePeriodVisuals with overlay color values
     */
    [[nodiscard]] TimePeriodVisuals getCurrentVisuals() const;

private:
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

    // Current state
    TimePeriod m_currentPeriod{TimePeriod::Day};
    TimePeriod m_previousPeriod{TimePeriod::Day};
};

#endif // DAY_NIGHT_CONTROLLER_HPP
