/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "utils/FrameProfiler.hpp"

#ifndef NDEBUG

#include "core/Logger.hpp"
#include "managers/FontManager.hpp"
#include "managers/UIConstants.hpp"
#include "managers/UIManager.hpp"
#include <SDL3/SDL_render.h>
#include <algorithm>
#include <format>

namespace HammerEngine {

// Convenience logging macros for profiler
#define PROFILER_WARN(msg) HAMMER_WARN("Profiler", msg)

FrameProfiler& FrameProfiler::Instance()
{
    static FrameProfiler instance;
    return instance;
}

const char* FrameProfiler::getPhaseName(FramePhase phase)
{
    switch (phase) {
    case FramePhase::Events: return "Events";
    case FramePhase::Update: return "Update";
    case FramePhase::Render: return "Render";
    case FramePhase::Present: return "Present";
    default: return "Unknown";
    }
}

const char* FrameProfiler::getManagerName(ManagerPhase mgr)
{
    switch (mgr) {
    case ManagerPhase::Event: return "Event";
    case ManagerPhase::GameState: return "GameState";
    case ManagerPhase::AI: return "AI";
    case ManagerPhase::Particle: return "Particle";
    case ManagerPhase::Pathfinder: return "Pathfinder";
    case ManagerPhase::Collision: return "Collision";
    case ManagerPhase::BackgroundSim: return "BackgroundSim";
    default: return "Unknown";
    }
}

ManagerPhase FrameProfiler::findWorstManager() const
{
    auto maxIt = std::max_element(m_managerTimes.begin(), m_managerTimes.end());
    return static_cast<ManagerPhase>(std::distance(m_managerTimes.begin(), maxIt));
}

const char* FrameProfiler::getRenderPhaseName(RenderPhase phase)
{
    switch (phase) {
    case RenderPhase::BeginScene: return "BeginScene";
    case RenderPhase::WorldTiles: return "WorldTiles";
    case RenderPhase::Entities: return "Entities";
    case RenderPhase::EndScene: return "EndScene";
    case RenderPhase::UI: return "UI";
    default: return "Unknown";
    }
}

RenderPhase FrameProfiler::findWorstRenderPhase() const
{
    auto maxIt = std::max_element(m_renderTimes.begin(), m_renderTimes.end());
    return static_cast<RenderPhase>(std::distance(m_renderTimes.begin(), maxIt));
}

void FrameProfiler::beginFrame()
{
    m_frameStart = Clock::now();

    // Reset timing arrays for this frame
    m_phaseTimes.fill(0.0);
    m_managerTimes.fill(0.0);
    m_renderTimes.fill(0.0);
}

void FrameProfiler::endFrame()
{
    auto frameEnd = Clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(frameEnd - m_frameStart).count();
    m_lastFrameTimeMs = totalMs;
    ++m_frameCount;

    // Decrement suppression counter (skip hitch logging during state transitions)
    if (m_suppressCount > 0) {
        --m_suppressCount;
        return;  // Skip hitch detection while suppressed
    }

    // Check for hitch
    if (totalMs > m_thresholdMs) {
        ++m_hitchCount;

        // Get all phase times
        double eventsTime = m_phaseTimes[static_cast<size_t>(FramePhase::Events)];
        double updateTime = m_phaseTimes[static_cast<size_t>(FramePhase::Update)];
        double renderTime = m_phaseTimes[static_cast<size_t>(FramePhase::Render)];
        double presentTime = m_phaseTimes[static_cast<size_t>(FramePhase::Present)];

        // Find worst phase
        FramePhase cause = FramePhase::Events;
        double maxPhaseTime = eventsTime;

        if (updateTime > maxPhaseTime) {
            cause = FramePhase::Update;
            maxPhaseTime = updateTime;
        }
        if (renderTime > maxPhaseTime) {
            cause = FramePhase::Render;
            maxPhaseTime = renderTime;
        }
        if (presentTime > maxPhaseTime) {
            cause = FramePhase::Present;
        }

        // Store for overlay
        m_lastHitchCause = cause;
        m_hadRecentHitch = true;
        m_lastHitchFrame = m_frameCount;

        // Log the hitch with all phases
        PROFILER_WARN(std::format("[HITCH] Frame {}: {:.1f}ms > {:.1f}ms threshold",
                                   m_frameCount, totalMs, m_thresholdMs));

        // Log each phase, marking the cause
        auto logPhase = [&](FramePhase phase, double time, const char* name) {
            if (phase == cause) {
                PROFILER_WARN(std::format("  {}: {:.1f}ms  <-- CAUSE", name, time));
            } else {
                PROFILER_WARN(std::format("  {}: {:.1f}ms", name, time));
            }
        };

        logPhase(FramePhase::Present, presentTime, "Present");
        logPhase(FramePhase::Render, renderTime, "Render");
        logPhase(FramePhase::Update, updateTime, "Update");
        logPhase(FramePhase::Events, eventsTime, "Events");

        // Show manager breakdown for update hitches
        if (cause == FramePhase::Update) {
            ManagerPhase worstMgr = findWorstManager();
            m_lastHitchManager = worstMgr;

            double worstTime = m_managerTimes[static_cast<size_t>(worstMgr)];
            PROFILER_WARN(std::format("    {}: {:.1f}ms  <-- WORST",
                                       getManagerName(worstMgr), worstTime));

            for (size_t i = 0; i < static_cast<size_t>(ManagerPhase::COUNT); ++i) {
                if (static_cast<ManagerPhase>(i) != worstMgr && m_managerTimes[i] > 1.0) {
                    PROFILER_WARN(std::format("    {}: {:.1f}ms",
                                               getManagerName(static_cast<ManagerPhase>(i)),
                                               m_managerTimes[i]));
                }
            }
        }

        // Show render breakdown for render hitches
        if (cause == FramePhase::Render || cause == FramePhase::Present) {
            RenderPhase worstRender = findWorstRenderPhase();
            double worstRenderTime = m_renderTimes[static_cast<size_t>(worstRender)];

            if (worstRenderTime > 0.1) {  // Only show if meaningful time
                PROFILER_WARN(std::format("    {}: {:.1f}ms  <-- WORST RENDER",
                                           getRenderPhaseName(worstRender), worstRenderTime));

                for (size_t i = 0; i < static_cast<size_t>(RenderPhase::COUNT); ++i) {
                    if (static_cast<RenderPhase>(i) != worstRender && m_renderTimes[i] > 0.1) {
                        PROFILER_WARN(std::format("    {}: {:.1f}ms",
                                                   getRenderPhaseName(static_cast<RenderPhase>(i)),
                                                   m_renderTimes[i]));
                    }
                }
            }
        }
    }

    // Clear recent hitch flag after a few frames
    if (m_hadRecentHitch && (m_frameCount - m_lastHitchFrame) > 60) {
        m_hadRecentHitch = false;
    }
}

void FrameProfiler::beginPhase(FramePhase phase)
{
    m_phaseStarts[static_cast<size_t>(phase)] = Clock::now();
}

void FrameProfiler::endPhase(FramePhase phase)
{
    auto now = Clock::now();
    auto start = m_phaseStarts[static_cast<size_t>(phase)];
    m_phaseTimes[static_cast<size_t>(phase)] =
        std::chrono::duration<double, std::milli>(now - start).count();
}

void FrameProfiler::beginManager(ManagerPhase mgr)
{
    m_managerStarts[static_cast<size_t>(mgr)] = Clock::now();
}

void FrameProfiler::endManager(ManagerPhase mgr)
{
    auto now = Clock::now();
    auto start = m_managerStarts[static_cast<size_t>(mgr)];
    m_managerTimes[static_cast<size_t>(mgr)] =
        std::chrono::duration<double, std::milli>(now - start).count();
}

void FrameProfiler::beginRender(RenderPhase phase)
{
    m_renderStarts[static_cast<size_t>(phase)] = Clock::now();
}

void FrameProfiler::endRender(RenderPhase phase)
{
    auto now = Clock::now();
    auto start = m_renderStarts[static_cast<size_t>(phase)];
    m_renderTimes[static_cast<size_t>(phase)] =
        std::chrono::duration<double, std::milli>(now - start).count();
}

void FrameProfiler::renderOverlay(SDL_Renderer* /*renderer*/, FontManager* /*fontMgr*/)
{
    auto& uiMgr = UIManager::Instance();

    // Handle overlay visibility state changes
    if (m_overlayVisible && !m_overlayCreated) {
        createOverlayComponents();
        m_overlayCreated = true;
    } else if (!m_overlayVisible && m_overlayCreated) {
        destroyOverlayComponents();
        m_overlayCreated = false;
        return;
    }

    if (!m_overlayVisible) {
        return;
    }

    // Update text content each frame via setText()
    updateOverlayText();

    uiMgr.setText("profiler_frame", m_frameText);
    uiMgr.setText("profiler_present", m_presentText);
    uiMgr.setText("profiler_render", m_renderText);
    uiMgr.setText("profiler_update", m_updateText);
    uiMgr.setText("profiler_events", m_eventsText);
    uiMgr.setText("profiler_threshold", m_thresholdText);
    uiMgr.setText("profiler_hitch", m_hitchText);
}

void FrameProfiler::createOverlayComponents()
{
    auto& ui = UIManager::Instance();

    if (ui.getLogicalWidth() <= 0 || ui.getLogicalHeight() <= 0) {
        return;
    }

    // Use UIConstants for all dimensions
    constexpr int W = UIConstants::PROFILER_OVERLAY_WIDTH;
    constexpr int H = UIConstants::PROFILER_OVERLAY_HEIGHT;
    constexpr int M = UIConstants::BOTTOM_RIGHT_OFFSET_X;  // Use standard bottom-right offset
    constexpr int LINE_H = UIConstants::PROFILER_LINE_HEIGHT;
    constexpr int PAD = UIConstants::DEFAULT_COMPONENT_PADDING;
    constexpr int LABEL_W = W - (PAD * 2);

    // Panel at bottom-right using UIManager helper
    ui.createPanelAtBottomRight("profiler_panel", W, H);
    UIStyle panelStyle;
    panelStyle.backgroundColor = {0, 0, 0, 200};
    panelStyle.borderColor = {80, 80, 80, 255};
    panelStyle.borderWidth = UIConstants::BORDER_WIDTH_NORMAL;
    ui.setStyle("profiler_panel", panelStyle);
    ui.setComponentZOrder("profiler_panel", UIConstants::PROFILER_ZORDER_PANEL);

    // Label style
    UIStyle labelStyle;
    labelStyle.textColor = {200, 200, 200, 255};
    labelStyle.backgroundColor = {0, 0, 0, 0};
    labelStyle.textAlign = UIAlignment::LEFT;
    labelStyle.fontID = std::string(UIConstants::FONT_UI);

    // Create labels inside panel
    // For BOTTOM_RIGHT: y = screenH - height - offsetY
    // Panel top is at: screenH - H - M = screenH - 160
    // First label top should be: screenH - 160 + PAD = screenH - 152
    // So offsetY for label = screenH - labelH - labelY = screenH - 22 - (screenH - 152) = 130
    // For row N (0-indexed): offsetY = H + M - PAD - LINE_H - (N * LINE_H) = H + M - PAD - (N+1)*LINE_H

    constexpr int BASE_OFFSET = H + M - PAD;  // 150 + 10 - 8 = 152

    ui.createLabelAtBottomRight("profiler_frame", "Frame: --", LABEL_W, LINE_H, M + PAD, BASE_OFFSET - 1*LINE_H);
    ui.setStyle("profiler_frame", labelStyle);
    ui.setComponentZOrder("profiler_frame", UIConstants::PROFILER_ZORDER_LABEL);

    ui.createLabelAtBottomRight("profiler_present", "Present: --", LABEL_W, LINE_H, M + PAD, BASE_OFFSET - 2*LINE_H);
    ui.setStyle("profiler_present", labelStyle);
    ui.setComponentZOrder("profiler_present", UIConstants::PROFILER_ZORDER_LABEL);

    ui.createLabelAtBottomRight("profiler_render", "Render: --", LABEL_W, LINE_H, M + PAD, BASE_OFFSET - 3*LINE_H);
    ui.setStyle("profiler_render", labelStyle);
    ui.setComponentZOrder("profiler_render", UIConstants::PROFILER_ZORDER_LABEL);

    ui.createLabelAtBottomRight("profiler_update", "Update: --", LABEL_W, LINE_H, M + PAD, BASE_OFFSET - 4*LINE_H);
    ui.setStyle("profiler_update", labelStyle);
    ui.setComponentZOrder("profiler_update", UIConstants::PROFILER_ZORDER_LABEL);

    ui.createLabelAtBottomRight("profiler_events", "Events: --", LABEL_W, LINE_H, M + PAD, BASE_OFFSET - 5*LINE_H);
    ui.setStyle("profiler_events", labelStyle);
    ui.setComponentZOrder("profiler_events", UIConstants::PROFILER_ZORDER_LABEL);

    ui.createLabelAtBottomRight("profiler_threshold", "Threshold: --", LABEL_W, LINE_H, M + PAD, BASE_OFFSET - 6*LINE_H);
    ui.setStyle("profiler_threshold", labelStyle);
    ui.setComponentZOrder("profiler_threshold", UIConstants::PROFILER_ZORDER_LABEL);

    ui.createLabelAtBottomRight("profiler_hitch", "", LABEL_W, LINE_H, M + PAD, BASE_OFFSET - 7*LINE_H);
    ui.setStyle("profiler_hitch", labelStyle);
    ui.setComponentZOrder("profiler_hitch", UIConstants::PROFILER_ZORDER_LABEL);
}

void FrameProfiler::destroyOverlayComponents()
{
    auto& uiMgr = UIManager::Instance();
    uiMgr.removeComponent("profiler_panel");
    uiMgr.removeComponent("profiler_frame");
    uiMgr.removeComponent("profiler_present");
    uiMgr.removeComponent("profiler_render");
    uiMgr.removeComponent("profiler_update");
    uiMgr.removeComponent("profiler_events");
    uiMgr.removeComponent("profiler_threshold");
    uiMgr.removeComponent("profiler_hitch");
}

void FrameProfiler::updateOverlayText()
{
    // Frame line
    m_frameText = std::format("Frame: {:.1f}ms | Hitches: {}",
                               m_lastFrameTimeMs, m_hitchCount);

    // Phase times
    double eventsTime = m_phaseTimes[static_cast<size_t>(FramePhase::Events)];
    double updateTime = m_phaseTimes[static_cast<size_t>(FramePhase::Update)];
    double renderTime = m_phaseTimes[static_cast<size_t>(FramePhase::Render)];
    double presentTime = m_phaseTimes[static_cast<size_t>(FramePhase::Present)];

    // Find worst manager
    ManagerPhase worstMgr = findWorstManager();
    double worstMgrTime = m_managerTimes[static_cast<size_t>(worstMgr)];

    // Present with cause marker (vsync wait)
    if (m_hadRecentHitch && m_lastHitchCause == FramePhase::Present) {
        m_presentText = std::format("PRESENT: {:.1f}ms <-", presentTime);
    } else {
        m_presentText = std::format("Present: {:.1f}ms", presentTime);
    }

    // Render with cause marker
    if (m_hadRecentHitch && m_lastHitchCause == FramePhase::Render) {
        m_renderText = std::format("RENDER: {:.1f}ms <-", renderTime);
    } else {
        m_renderText = std::format("Render: {:.1f}ms", renderTime);
    }

    // Update with cause marker
    if (m_hadRecentHitch && m_lastHitchCause == FramePhase::Update) {
        m_updateText = std::format("UPDATE: {:.1f}ms [{}: {:.1f}ms] <-",
                                    updateTime, getManagerName(worstMgr), worstMgrTime);
    } else {
        m_updateText = std::format("Update: {:.1f}ms [{}: {:.1f}ms]",
                                    updateTime, getManagerName(worstMgr), worstMgrTime);
    }

    // Events with cause marker
    if (m_hadRecentHitch && m_lastHitchCause == FramePhase::Events) {
        m_eventsText = std::format("EVENTS: {:.1f}ms <-", eventsTime);
    } else {
        m_eventsText = std::format("Events: {:.1f}ms", eventsTime);
    }

    // Threshold
    m_thresholdText = std::format("Threshold: {:.1f}ms", m_thresholdMs);

    // Hitch info
    if (m_hadRecentHitch) {
        m_hitchText = std::format("Cause: {} ({})",
                                   getPhaseName(m_lastHitchCause),
                                   m_lastHitchCause == FramePhase::Update
                                       ? getManagerName(m_lastHitchManager)
                                       : "-");
    } else {
        m_hitchText = "";
    }
}

}  // namespace HammerEngine

#endif  // NDEBUG
