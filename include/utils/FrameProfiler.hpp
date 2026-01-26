/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef FRAME_PROFILER_HPP
#define FRAME_PROFILER_HPP

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

// Forward declarations for overlay rendering
struct SDL_Renderer;
class FontManager;

namespace HammerEngine {

/**
 * @brief Frame phases for high-level profiling
 */
enum class FramePhase : uint8_t {
    Events = 0,
    Update,
    Render,
    COUNT
};

/**
 * @brief Manager phases for detailed update profiling
 */
enum class ManagerPhase : uint8_t {
    Event = 0,
    GameState,
    AI,
    Particle,
    Pathfinder,
    Collision,
    BackgroundSim,
    COUNT
};

#ifndef NDEBUG

/**
 * @brief Debug-only frame profiler for detecting and reporting hitches
 *
 * Automatically logs when frame time exceeds threshold with detailed breakdown:
 * - Which phase (Events/Update/Render) caused the hitch
 * - If Update, which manager was the worst offender
 *
 * Press F3 to toggle an overlay showing live frame timing.
 */
class FrameProfiler {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    /**
     * @brief Gets the singleton instance
     */
    static FrameProfiler& Instance();

    /**
     * @brief Sets the hitch detection threshold in milliseconds
     * @param ms Threshold in ms (default: 20ms for 60fps target)
     */
    void setThresholdMs(double ms) { m_thresholdMs = ms; }

    /**
     * @brief Gets the current threshold
     */
    double getThresholdMs() const { return m_thresholdMs; }

    /**
     * @brief Toggles the F3 debug overlay
     */
    void toggleOverlay() { m_overlayVisible = !m_overlayVisible; }

    /**
     * @brief Checks if the overlay is visible
     */
    bool isOverlayVisible() const { return m_overlayVisible; }

    /**
     * @brief Suppresses hitch detection for N frames
     * @param frameCount Number of frames to suppress (default: 5)
     *
     * Use during state transitions, resource loading, or engine init
     * to avoid logging expected hitches.
     */
    void suppressFrames(uint32_t frameCount = 5) { m_suppressCount = frameCount; }

    /**
     * @brief Checks if hitch detection is currently suppressed
     */
    bool isSuppressed() const { return m_suppressCount > 0; }

    /**
     * @brief Marks the beginning of a new frame
     */
    void beginFrame();

    /**
     * @brief Marks the end of a frame, checks for hitches, logs if needed
     */
    void endFrame();

    /**
     * @brief Begins timing a frame phase
     * @param phase The phase to begin timing
     */
    void beginPhase(FramePhase phase);

    /**
     * @brief Ends timing a frame phase
     * @param phase The phase to end timing
     */
    void endPhase(FramePhase phase);

    /**
     * @brief Begins timing a manager update
     * @param mgr The manager phase to begin timing
     */
    void beginManager(ManagerPhase mgr);

    /**
     * @brief Ends timing a manager update
     * @param mgr The manager phase to end timing
     */
    void endManager(ManagerPhase mgr);

    /**
     * @brief Renders the debug overlay
     * @param renderer SDL renderer
     * @param fontMgr Font manager for text rendering
     */
    void renderOverlay(SDL_Renderer* renderer, FontManager* fontMgr);

    /**
     * @brief Gets the total hitch count since startup
     */
    uint32_t getHitchCount() const { return m_hitchCount; }

    /**
     * @brief Gets the current frame count
     */
    uint64_t getFrameCount() const { return m_frameCount; }

    /**
     * @brief Gets the last frame's total time in ms
     */
    double getLastFrameTimeMs() const { return m_lastFrameTimeMs; }

    /**
     * @brief Gets time for a specific phase from last frame
     */
    double getPhaseTimeMs(FramePhase phase) const {
        return m_phaseTimes[static_cast<size_t>(phase)];
    }

    /**
     * @brief Gets time for a specific manager from last frame
     */
    double getManagerTimeMs(ManagerPhase mgr) const {
        return m_managerTimes[static_cast<size_t>(mgr)];
    }

private:
    FrameProfiler() = default;
    ~FrameProfiler() = default;
    FrameProfiler(const FrameProfiler&) = delete;
    FrameProfiler& operator=(const FrameProfiler&) = delete;

    static const char* getPhaseName(FramePhase phase);
    static const char* getManagerName(ManagerPhase mgr);
    ManagerPhase findWorstManager() const;

    // Timing data
    TimePoint m_frameStart{};
    std::array<TimePoint, static_cast<size_t>(FramePhase::COUNT)> m_phaseStarts{};
    std::array<TimePoint, static_cast<size_t>(ManagerPhase::COUNT)> m_managerStarts{};
    std::array<double, static_cast<size_t>(FramePhase::COUNT)> m_phaseTimes{};
    std::array<double, static_cast<size_t>(ManagerPhase::COUNT)> m_managerTimes{};

    // Configuration
    double m_thresholdMs{20.0};  // 1.5x of 16.67ms (60fps) by default
    uint32_t m_suppressCount{0}; // Frames to skip hitch detection

    // Statistics
    uint64_t m_frameCount{0};
    uint32_t m_hitchCount{0};
    double m_lastFrameTimeMs{0.0};

    // Last hitch info for overlay
    FramePhase m_lastHitchCause{FramePhase::Events};
    ManagerPhase m_lastHitchManager{ManagerPhase::Event};
    bool m_hadRecentHitch{false};
    uint64_t m_lastHitchFrame{0};

    // Overlay state
    bool m_overlayVisible{false};
    bool m_overlayCreated{false};

    // Text buffers for UI (avoid per-frame allocations)
    std::string m_frameText{};
    std::string m_updateText{};
    std::string m_renderText{};
    std::string m_eventsText{};
    std::string m_thresholdText{};
    std::string m_hitchText{};

    // Internal helpers for overlay management
    void createOverlayComponents();
    void destroyOverlayComponents();
    void updateOverlayText();
};

/**
 * @brief RAII scoped timer for frame phases
 */
class ScopedPhaseTimer {
public:
    explicit ScopedPhaseTimer(FramePhase phase) : m_phase(phase) {
        FrameProfiler::Instance().beginPhase(m_phase);
    }
    ~ScopedPhaseTimer() {
        FrameProfiler::Instance().endPhase(m_phase);
    }
    ScopedPhaseTimer(const ScopedPhaseTimer&) = delete;
    ScopedPhaseTimer& operator=(const ScopedPhaseTimer&) = delete;
private:
    FramePhase m_phase;
};

/**
 * @brief RAII scoped timer for manager phases
 */
class ScopedManagerTimer {
public:
    explicit ScopedManagerTimer(ManagerPhase mgr) : m_mgr(mgr) {
        FrameProfiler::Instance().beginManager(m_mgr);
    }
    ~ScopedManagerTimer() {
        FrameProfiler::Instance().endManager(m_mgr);
    }
    ScopedManagerTimer(const ScopedManagerTimer&) = delete;
    ScopedManagerTimer& operator=(const ScopedManagerTimer&) = delete;
private:
    ManagerPhase m_mgr;
};

// Debug macros - compile to actual profiling
#define PROFILE_FRAME_BEGIN() HammerEngine::FrameProfiler::Instance().beginFrame()
#define PROFILE_FRAME_END() HammerEngine::FrameProfiler::Instance().endFrame()
#define PROFILE_PHASE(p) HammerEngine::ScopedPhaseTimer _scopedPhaseTimer##__LINE__(p)
#define PROFILE_MANAGER(m) HammerEngine::ScopedManagerTimer _scopedManagerTimer##__LINE__(m)

#else  // NDEBUG - Release build

// Stub class for release builds - completely empty
class FrameProfiler {
public:
    static FrameProfiler& Instance() {
        static FrameProfiler instance;
        return instance;
    }
    void setThresholdMs(double) {}
    double getThresholdMs() const { return 0.0; }
    void toggleOverlay() {}
    bool isOverlayVisible() const { return false; }
    void suppressFrames(uint32_t = 5) {}
    bool isSuppressed() const { return false; }
    void beginFrame() {}
    void endFrame() {}
    void beginPhase(FramePhase) {}
    void endPhase(FramePhase) {}
    void beginManager(ManagerPhase) {}
    void endManager(ManagerPhase) {}
    void renderOverlay(SDL_Renderer*, FontManager*) {}
    uint32_t getHitchCount() const { return 0; }
    uint64_t getFrameCount() const { return 0; }
    double getLastFrameTimeMs() const { return 0.0; }
    double getPhaseTimeMs(FramePhase) const { return 0.0; }
    double getManagerTimeMs(ManagerPhase) const { return 0.0; }
};

// Release macros - compile to nothing
#define PROFILE_FRAME_BEGIN() ((void)0)
#define PROFILE_FRAME_END() ((void)0)
#define PROFILE_PHASE(p) ((void)0)
#define PROFILE_MANAGER(m) ((void)0)

#endif  // NDEBUG

}  // namespace HammerEngine

#endif  // FRAME_PROFILER_HPP
