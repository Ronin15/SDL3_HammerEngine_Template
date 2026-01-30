/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/world/DayNightController.hpp"
#include "managers/GameTimeManager.hpp"
#include "core/Logger.hpp"
#include <format>
#include <cmath>

#ifdef USE_SDL3_GPU
#include "gpu/GPURenderer.hpp"
#endif

void DayNightController::subscribe()
{
    if (checkAlreadySubscribed()) {
        return;
    }

    auto& eventMgr = EventManager::Instance();

    // Subscribe to Time events to detect hour changes
    auto timeToken = eventMgr.registerHandlerWithToken(
        EventTypeId::Time,
        [this](const EventData& data) { onTimeEvent(data); }
    );
    addHandlerToken(timeToken);

    // Initialize to current time period
    float currentHour = GameTimeManager::Instance().getGameHour();
    m_currentPeriod = hourToTimePeriod(currentHour);
    m_previousPeriod = m_currentPeriod;

    // Initialize lighting to current period (no interpolation needed at start)
    auto visuals = TimePeriodVisuals::getForPeriod(m_currentPeriod);
    m_currentR = m_targetR = static_cast<float>(visuals.overlayR);
    m_currentG = m_targetG = static_cast<float>(visuals.overlayG);
    m_currentB = m_targetB = static_cast<float>(visuals.overlayB);
    m_currentA = m_targetA = static_cast<float>(visuals.overlayA);

    // Update GPU with initial lighting state
    updateGPULighting();

    // Dispatch initial event so subscribers know the current state
    // This allows GamePlayState (and other subscribers) to set up ambient particles
    auto event = std::make_shared<TimePeriodChangedEvent>(m_currentPeriod, m_previousPeriod, visuals);
    eventMgr.dispatchEvent(event, EventManager::DispatchMode::Deferred);

    setSubscribed(true);
    DAYNIGHT_INFO(std::format("Subscribed to time events, period: {}",
                getCurrentPeriodString()));
}

void DayNightController::onTimeEvent(const EventData& data)
{
    if (!data.event) {
        return;
    }

    // Use TimeEventType enum to check event type
    auto timeEvent = std::static_pointer_cast<TimeEvent>(data.event);
    TimeEventType eventType = timeEvent->getTimeEventType();

    // Only care about hour changes for day/night transitions
    if (eventType != TimeEventType::HourChanged) {
        return;
    }

    auto hourEvent = std::static_pointer_cast<HourChangedEvent>(data.event);
    int hour = hourEvent->getHour();

    // Determine current time period from hour
    TimePeriod newPeriod = hourToTimePeriod(static_cast<float>(hour));

    // Only transition if period actually changed
    if (newPeriod != m_currentPeriod) {
        transitionToPeriod(newPeriod);
    }
}

void DayNightController::transitionToPeriod(TimePeriod newPeriod)
{
    m_previousPeriod = m_currentPeriod;
    m_currentPeriod = newPeriod;

    // Set target lighting values for interpolation
    auto visuals = TimePeriodVisuals::getForPeriod(m_currentPeriod);
    m_targetR = static_cast<float>(visuals.overlayR);
    m_targetG = static_cast<float>(visuals.overlayG);
    m_targetB = static_cast<float>(visuals.overlayB);
    m_targetA = static_cast<float>(visuals.overlayA);

    // Dispatch TimePeriodChangedEvent through EventManager
    // Subscribers (like GamePlayState) handle ambient particles
    auto event = std::make_shared<TimePeriodChangedEvent>(m_currentPeriod, m_previousPeriod, visuals);
    EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Deferred);

    DAYNIGHT_INFO(std::format("Transitioned to {}", getCurrentPeriodString()));
}

std::string_view DayNightController::getCurrentPeriodString() const
{
    switch (m_currentPeriod) {
        case TimePeriod::Morning: return "Morning";
        case TimePeriod::Day:     return "Day";
        case TimePeriod::Evening: return "Evening";
        case TimePeriod::Night:   return "Night";
        default:                  return "Unknown";
    }
}

std::string_view DayNightController::getCurrentPeriodDescription() const
{
    switch (m_currentPeriod) {
        case TimePeriod::Morning: return "Dawn approaches";
        case TimePeriod::Day:     return "The sun rises high";
        case TimePeriod::Evening: return "Dusk settles in";
        case TimePeriod::Night:   return "Night falls";
        default:                  return "Time passes";
    }
}

TimePeriodVisuals DayNightController::getCurrentVisuals() const
{
    return TimePeriodVisuals::getForPeriod(m_currentPeriod);
}

TimePeriod DayNightController::hourToTimePeriod(float hour)
{
    // Time periods matching GameTimeManager::getTimeOfDayName() logic:
    // Morning: 5:00 - 8:00
    // Day:     8:00 - 17:00
    // Evening: 17:00 - 21:00
    // Night:   21:00 - 5:00

    if (hour >= 5.0f && hour < 8.0f) {
        return TimePeriod::Morning;
    } else if (hour >= 8.0f && hour < 17.0f) {
        return TimePeriod::Day;
    } else if (hour >= 17.0f && hour < 21.0f) {
        return TimePeriod::Evening;
    } else {
        return TimePeriod::Night;
    }
}

void DayNightController::update(float deltaTime)
{
    // Exponential smoothing for natural-feeling transitions
    // lerpFactor approaches 1.0 over TRANSITION_DURATION seconds
    float lerpFactor = 1.0f - std::exp(-deltaTime * (3.0f / TRANSITION_DURATION));

    // Interpolate current values toward target
    m_currentR += (m_targetR - m_currentR) * lerpFactor;
    m_currentG += (m_targetG - m_currentG) * lerpFactor;
    m_currentB += (m_targetB - m_currentB) * lerpFactor;
    m_currentA += (m_targetA - m_currentA) * lerpFactor;

    // Update GPU with interpolated lighting
    updateGPULighting();
}

void DayNightController::updateGPULighting()
{
#ifdef USE_SDL3_GPU
    auto& gpuRenderer = HammerEngine::GPURenderer::Instance();
    // Convert from 0-255 range to 0-1 range for shader
    gpuRenderer.setDayNightParams(
        m_currentR / 255.0f,
        m_currentG / 255.0f,
        m_currentB / 255.0f,
        m_currentA / 255.0f
    );
#endif
}
