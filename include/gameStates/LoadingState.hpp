/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef LOADING_STATE_HPP
#define LOADING_STATE_HPP

#include "gameStates/GameState.hpp"
#include "world/WorldData.hpp"
#include <atomic>
#include <future>
#include <string>

/**
 * @brief Loading screen state that handles async world generation
 *
 * LoadingState provides a clean, non-blocking loading screen that:
 * - Runs world generation on background thread (ThreadSystem)
 * - Displays progress updates via UI
 * - Keeps render loop responsive (no manual RenderClear/Present)
 * - Auto-transitions to target state when loading completes
 * - SDL3_GPU compatible (all rendering through GameEngine::render())
 *
 * This is the industry-standard pattern used by Unity, Unreal, and Godot.
 *
 * Usage:
 *   auto* loadingState = dynamic_cast<LoadingState*>(gameStateManager->getState("LoadingState").get());
 *   loadingState->configure("TargetState", worldConfig);
 *   gameStateManager->pushState("LoadingState");
 */
class LoadingState : public GameState {
public:
    /**
     * @brief Default constructor for state registration
     */
    LoadingState() = default;

    /**
     * @brief Configure the loading state before pushing it
     * @param targetStateName State to transition to after loading completes
     * @param worldConfig World generation configuration
     */
    void configure(const std::string& targetStateName,
                   const HammerEngine::WorldGenerationConfig& worldConfig);

    bool enter() override;
    void update(float deltaTime) override;
    void render(SDL_Renderer* renderer, float interpolationAlpha = 1.0f) override;
    void handleInput() override;
    bool exit() override;
    std::string getName() const override;

#ifdef USE_SDL3_GPU
    // GPU rendering support
    void recordGPUVertices(HammerEngine::GPURenderer& gpuRenderer,
                           float interpolationAlpha) override;
    void renderGPUUI(HammerEngine::GPURenderer& gpuRenderer,
                     SDL_GPURenderPass* swapchainPass) override;
    bool supportsGPURendering() const override { return true; }
#endif

    /**
     * @brief Get the last error message from failed loading
     * @return Error message string, empty if no error occurred
     */
    std::string getLastError() const;

    /**
     * @brief Check if an error occurred during loading
     * @return true if loading failed with an error, false otherwise
     */
    bool hasError() const;

private:
    // Target state to transition to after loading
    std::string m_targetStateName;

    // World generation configuration
    HammerEngine::WorldGenerationConfig m_worldConfig{};

    // Async loading state (thread-safe atomics)
    std::atomic<float> m_progress{0.0f};
    std::atomic<bool> m_loadComplete{false};
    std::atomic<bool> m_loadFailed{false};
    std::atomic<bool> m_waitingForPathfinding{false};
    std::atomic<bool> m_waitingForPrewarm{false};  // Pathfinding done, waiting for chunk prewarm
    std::atomic<bool> m_prewarmComplete{false};    // Chunk prewarm complete

    // Status message (mutex-protected for string safety)
    std::string m_statusText{"Initializing..."};
    mutable std::mutex m_statusMutex;

    // Error tracking (mutex-protected for string safety)
    std::string m_lastError;
    mutable std::mutex m_errorMutex;

    // Future for async world loading task
    std::future<bool> m_loadTask;

    // UI state
    bool m_uiInitialized{false};

    /**
     * @brief Start async world loading on ThreadSystem
     */
    void startAsyncWorldLoad();

    /**
     * @brief Update status text (thread-safe)
     */
    void setStatusText(const std::string& status);

    /**
     * @brief Get status text (thread-safe)
     */
    std::string getStatusText() const;

    /**
     * @brief Initialize loading screen UI
     */
    void initializeUI();

    /**
     * @brief Cleanup loading screen UI
     */
    void cleanupUI();
};

#endif // LOADING_STATE_HPP
