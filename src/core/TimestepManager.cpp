/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "core/TimestepManager.hpp"
#include <algorithm>
#include <chrono>

TimestepManager::TimestepManager(float targetFPS, float fixedTimestep)
    : m_targetFPS(targetFPS)
    , m_fixedTimestep(fixedTimestep)
    , m_targetFrameTime(1.0f / targetFPS)
    , m_accumulator(0.0)
    , m_lastFrameTimeMs(0)
    , m_lastDeltaSeconds(0.0)
    , m_currentFPS(0.0f)
    , m_smoothingAlpha(0.03f)
    , m_shouldRender(true)
    , m_firstFrame(true)
{
    auto currentTime = std::chrono::high_resolution_clock::now();
    m_frameStart = currentTime;
    m_lastFrameTime = currentTime;

    // Initialize fixed timestep state
    m_usingSoftwareFrameLimiting = false;
    m_explicitlySet = false;
}

void TimestepManager::startFrame() {
    auto currentTime = std::chrono::high_resolution_clock::now();
    
    if (m_firstFrame) {
        m_firstFrame = false;
        m_lastFrameTime = currentTime;
        m_frameStart = currentTime;
        return;
    }
    
    // Calculate frame delta time in seconds
    auto deltaTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime - m_lastFrameTime);
    double deltaTimeMs = static_cast<double>(deltaTimeNs.count()) / 1000000.0;
    m_lastFrameTime = currentTime;
    m_frameStart = currentTime;
    
    // Update frame time in milliseconds (for getFrameTimeMs() API)
    m_lastFrameTimeMs = static_cast<uint32_t>(deltaTimeMs);

    // Convert to seconds
    double deltaTime = deltaTimeMs / 1000.0;
    m_lastDeltaSeconds = deltaTime;  // Store high precision for FPS calculation

    // Mode-aware accumulator handling:
    // VSync mode: Normal accumulator pattern - add delta (clamped to prevent spiral)
    //             Allows catch-up updates under load, uses interpolation for smoothness
    // Software mode: Force exactly one update per frame by setting accumulator = timestep
    //                Since we control timing via SDL_DelayPrecise, no catch-up needed
    //                This eliminates timing jitter causing 0-update or 2-update frames
    if (m_usingSoftwareFrameLimiting) {
        // Set to exactly one timestep - guarantees one update, alpha = 0 after
        m_accumulator = m_fixedTimestep;
    } else {
        // VSync: Clamp delta to prevent spiral of death, then add to accumulator
        deltaTime = std::min(deltaTime, MAX_ACCUMULATOR);
        m_accumulator += deltaTime;
    }
    
    // Always render once per frame
    m_shouldRender = true;
    
    // Update FPS counter
    updateFPS();
}

bool TimestepManager::shouldUpdate() {
    // Accumulator pattern: run updates until accumulator is drained below threshold
    if (m_accumulator >= m_fixedTimestep) {
        m_accumulator -= m_fixedTimestep;
        return true;
    }
    return false;
}

bool TimestepManager::shouldRender() const {
    return m_shouldRender;
}

float TimestepManager::getUpdateDeltaTime() const {
    // Always return the fixed timestep for consistent game logic.
    return m_fixedTimestep;
}

double TimestepManager::getInterpolationAlpha() const {
    if (m_fixedTimestep > 0.0f) {
        double alpha = m_accumulator / m_fixedTimestep;
        return std::clamp(alpha, 0.0, 1.0);  // Prevent extrapolation during frame drops
    }
    return 1.0; // Default to 1.0 to avoid division by zero
}

void TimestepManager::endFrame() {
    // Mark render as completed for this frame
    m_shouldRender = false;

    // Limit frame rate
    limitFrameRate();
}

float TimestepManager::getCurrentFPS() const {
    return m_currentFPS;
}

float TimestepManager::getTargetFPS() const {
    return m_targetFPS;
}

uint32_t TimestepManager::getFrameTimeMs() const {
    return m_lastFrameTimeMs;
}

bool TimestepManager::isFrameTimeExcessive() const {
    // Consider frame time excessive if it's more than 2x target frame time
    return m_lastFrameTimeMs > static_cast<uint32_t>(m_targetFrameTime * 2000.0f);
}

void TimestepManager::setTargetFPS(float fps) {
    if (fps > 0.0f) {
        m_targetFPS = fps;
        m_targetFrameTime = 1.0f / fps;
    }
}

void TimestepManager::setFixedTimestep(float timestep) {
    if (timestep > 0.0f) {
        m_fixedTimestep = timestep;
    }
}

void TimestepManager::reset() {
    m_accumulator = 0.0;
    m_firstFrame = true;
    m_shouldRender = true;
    m_currentFPS = 0.0f;
    m_lastDeltaSeconds = 0.0;

    auto currentTime = std::chrono::high_resolution_clock::now();
    m_frameStart = currentTime;
    m_lastFrameTime = currentTime;

    // Preserve explicit software frame limiting settings during reset
    // Only reset if not explicitly configured by GameEngine
    if (!m_explicitlySet) {
        m_usingSoftwareFrameLimiting = false;
    }
}

void TimestepManager::updateFPS() {
    // EMA-based FPS calculation using high-precision delta time
    if (m_lastDeltaSeconds > 0.0) {
        float instantFPS = static_cast<float>(1.0 / m_lastDeltaSeconds);
        instantFPS = std::clamp(instantFPS, 0.1f, 1000.0f);

        if (m_currentFPS <= 0.0f) {
            m_currentFPS = instantFPS;
        } else {
            m_currentFPS = m_smoothingAlpha * instantFPS + (1.0f - m_smoothingAlpha) * m_currentFPS;
        }
    }
}

void TimestepManager::limitFrameRate() const {
    // If using hardware VSync, skip software limiting - VSync handles timing via SDL_RenderPresent()
    if (!m_usingSoftwareFrameLimiting) {
        return;
    }

    // Calculate absolute target end time for this frame
    // Using nanoseconds for maximum precision
    int64_t targetFrameNs = static_cast<int64_t>(m_targetFrameTime * 1e9);
    auto targetEndTime = m_frameStart + std::chrono::nanoseconds(targetFrameNs);

    // Calculate remaining time NOW (minimizes overhead between calculation and delay)
    auto now = std::chrono::high_resolution_clock::now();
    auto remainingNs = std::chrono::duration_cast<std::chrono::nanoseconds>(targetEndTime - now);

    // Only delay if we have time remaining
    if (remainingNs.count() > 0) {
        SDL_DelayPrecise(static_cast<Uint64>(remainingNs.count()));
    }
}

void TimestepManager::setSoftwareFrameLimiting(bool useSoftwareLimiting) const {
    m_usingSoftwareFrameLimiting = useSoftwareLimiting;
    m_explicitlySet = true;
    // Mode-aware accumulator logic is handled in startFrame()
}

void TimestepManager::preciseFrameWait(double targetFrameTimeMs) const {
    // Use SDL3's built-in precise delay which handles hybrid sleep+busy-wait internally.
    // This is more efficient and adaptive than our previous custom implementation:
    // - Adaptively adjusts busy-wait buffer based on actual OS sleep behavior
    // - Platform-optimized timing primitives
    // - Uses CPU pause hints during busy-wait to reduce power consumption
    // Available since SDL 3.2.0
    Uint64 targetNs = static_cast<Uint64>(targetFrameTimeMs * 1000000.0);
    SDL_DelayPrecise(targetNs);
}

