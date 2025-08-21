/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef TIMESTEP_MANAGER_HPP
#define TIMESTEP_MANAGER_HPP

#include <cstdint>
#include <chrono>
#include <SDL3/SDL.h>

/**
 * TimestepManager provides consistent game timing with simplified accumulator handling.
 * 
 * This separates update timing (fixed timestep for consistent physics/logic)
 * from render timing (variable timestep for smooth visuals).
 * 
 * Uses 1:1 frame-to-update mapping to eliminate timing drift and micro-stuttering
 * that can occur with traditional accumulator patterns.
 */
class TimestepManager {
public:
    /**
     * Constructor
     * @param targetFPS Target frames per second for rendering (e.g., 60.0f)
     * @param fixedTimestep Fixed timestep for updates in seconds (e.g., 1.0f/60.0f)
     */
    explicit TimestepManager(float targetFPS = 60.0f, float fixedTimestep = 1.0f/60.0f);

    /**
     * Call this at the start of each frame
     */
    void startFrame();

    /**
     * Returns true if an update should be performed with fixed timestep.
     * May return true multiple times per frame for catch-up.
     * @return true if update should run
     */
    bool shouldUpdate();

    /**
     * Returns true if rendering should be performed.
     * Typically once per frame.
     * @return true if render should run
     */
    bool shouldRender() const;

    /**
     * Gets the fixed delta time for updates.
     * Always returns the same value for consistent physics.
     * @return fixed timestep in seconds
     */
    float getUpdateDeltaTime() const;

    /**
     * Gets the interpolation factor (alpha) for smooth rendering between fixed updates.
     * This represents the fraction of time into the next fixed update step.
     * @return interpolation alpha value (typically between 0.0 and 1.0)
     */
    double getInterpolationAlpha() const;

    /**
     * Call this at the end of each frame.
     * Handles frame rate limiting via sleep/delay.
     */
    void endFrame();

    /**
     * Get current measured FPS
     * @return current frames per second
     */
    float getCurrentFPS() const;

    /**
     * Get target FPS
     * @return target frames per second
     */
    float getTargetFPS() const;

    /**
     * Get last frame time in milliseconds
     * @return frame time in ms
     */
    uint32_t getFrameTimeMs() const;

    /**
     * Check if the last frame exceeded target time significantly
     * @return true if frame time was excessive
     */
    bool isFrameTimeExcessive() const;

    /**
     * Set new target FPS (updates frame time target)
     * @param fps new target frames per second
     */
    void setTargetFPS(float fps);

    /**
     * Set new fixed timestep for updates
     * @param timestep new fixed timestep in seconds
     */
    void setFixedTimestep(float timestep);

    /**
     * Reset timing state (useful when pausing/unpausing)
     */
    void reset();

private:
    // Timing configuration
    float m_targetFPS;                    // Target frames per second for rendering
    float m_fixedTimestep;               // Fixed timestep for updates (seconds)
    float m_targetFrameTime;             // Target frame time (1/targetFPS)
    
    // Frame timing (std::chrono for high precision, consistent with GameLoop)
    std::chrono::high_resolution_clock::time_point m_frameStart;
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
    
    // Simplified timing pattern (eliminates accumulator drift)
    double m_accumulator;                // Simple frame timing state
    static constexpr double MAX_ACCUMULATOR = 0.25; // Unused (kept for compatibility)
    
    // Frame statistics
    uint32_t m_lastFrameTimeMs;         // Last frame duration in milliseconds
    float m_currentFPS;                 // Current measured FPS
    uint32_t m_frameCount;              // Frame counter for FPS calculation
    std::chrono::high_resolution_clock::time_point m_fpsLastUpdate;             // Last FPS update time
    
    // State flags
    bool m_shouldRender;                // True when render should happen this frame
    bool m_firstFrame;                  // True for the very first frame
    
    // Fixed timestep for software frame limiting
    mutable bool m_usingSoftwareFrameLimiting = false;
    mutable bool m_explicitlySet = false;
    
    // Helper methods
    void updateFPS();
    void limitFrameRate() const;
    
public:
    /**
     * Explicitly set software frame limiting mode (called from GameEngine)
     * @param useSoftwareLimiting true to force fixed timestep mode
     */
    void setSoftwareFrameLimiting(bool useSoftwareLimiting);
};

#endif // TIMESTEP_MANAGER_HPP