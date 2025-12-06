/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/LoadingState.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "managers/GameStateManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/WorldManager.hpp"
#include "managers/PathfinderManager.hpp"
#include <random>

void LoadingState::configure(const std::string& targetStateName,
                             const HammerEngine::WorldGenerationConfig& worldConfig) {
    m_targetStateName = targetStateName;
    m_worldConfig = worldConfig;

    // Reset state for reuse
    m_progress.store(0.0f, std::memory_order_release);
    m_loadComplete.store(false, std::memory_order_release);
    m_loadFailed.store(false, std::memory_order_release);
    m_waitingForPathfinding.store(false, std::memory_order_release);
    setStatusText("Initializing...");

    // Clear any previous error
    {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError.clear();
    }
}

bool LoadingState::enter() {
    // Validate that LoadingState was properly configured
    if (m_targetStateName.empty()) {
        GAMESTATE_ERROR("LoadingState not configured - call configure() before pushing state");
        return false;
    }

    GAMESTATE_INFO("Entering LoadingState - Target: " + m_targetStateName);

    // Initialize loading screen UI
    initializeUI();

    // Start async world loading
    startAsyncWorldLoad();

    return true;
}

void LoadingState::update([[maybe_unused]] float deltaTime) {
    // Fast path: Check with relaxed ordering first (no memory barrier)
    if (m_loadComplete.load(std::memory_order_relaxed)) {
        // Slow path: Verify with acquire barrier only when likely true
        if (m_loadComplete.load(std::memory_order_acquire)) {
            // Check if we need to wait for pathfinding grid
            if (!m_waitingForPathfinding.load(std::memory_order_acquire)) {
                // World loading just completed - now wait for pathfinding
                m_waitingForPathfinding.store(true, std::memory_order_release);
                setStatusText("Preparing pathfinding grid...");
                GAMESTATE_INFO("World loading complete - waiting for pathfinding grid");
                return; // Wait for next frame to check grid readiness
            }

            // Check if pathfinding grid is ready
            const auto& pathfinderManager = PathfinderManager::Instance();
            if (!pathfinderManager.isGridReady()) {
                // Grid still building - keep waiting
                return;
            }

            // Both world and pathfinding grid are ready - proceed with transition
            if (m_loadFailed.load(std::memory_order_acquire)) {
                std::string errorMsg = "World loading failed - transitioning anyway";
                GAMESTATE_ERROR(errorMsg);

                // Store error if not already set by the loadTask lambda
                if (!hasError()) {
                    std::lock_guard<std::mutex> lock(m_errorMutex);
                    m_lastError = errorMsg;
                }
                // Continue to target state even on failure (matches current behavior)
            } else {
                GAMESTATE_INFO("World and pathfinding ready - transitioning to " + m_targetStateName);
            }

            // Transition to target state
            const auto& gameEngine = GameEngine::Instance();
            auto* gameStateManager = gameEngine.getGameStateManager();
            if (gameStateManager && gameStateManager->hasState(m_targetStateName)) {
                gameStateManager->changeState(m_targetStateName);
            } else {
                std::string errorMsg = "Target state not found: " + m_targetStateName;
                GAMESTATE_ERROR(errorMsg);

                // Store error for diagnostic purposes
                {
                    std::lock_guard<std::mutex> lock(m_errorMutex);
                    m_lastError = errorMsg;
                }
            }
        }
    }
}

void LoadingState::render(SDL_Renderer* renderer) {
    // All rendering happens through GameEngine::render() -> this method
    // No manual SDL_RenderClear() or SDL_RenderPresent() calls needed!

    auto& ui = UIManager::Instance();

    // Update progress bar
    float currentProgress = m_progress.load(std::memory_order_acquire);
    ui.updateProgressBar("loading_progress", currentProgress);

    // Update status text
    ui.setText("loading_status", getStatusText());

    // Actually render the UI to the screen!
    ui.render(renderer);
}

void LoadingState::handleInput() {
    // LoadingState doesn't accept input - loading must complete
}

bool LoadingState::exit() {
    GAMESTATE_INFO("Exiting LoadingState");

    // Cleanup loading UI
    cleanupUI();

    // Wait for async task to complete if still running
    if (m_loadTask.valid()) {
        try {
            m_loadTask.wait();
        } catch (const std::exception& e) {
            std::string errorMsg = "Exception while waiting for load task: " + std::string(e.what());
            GAMESTATE_ERROR(errorMsg);

            // Store error for diagnostic purposes
            {
                std::lock_guard<std::mutex> lock(m_errorMutex);
                m_lastError = errorMsg;
            }
        }
    }

    return true;
}

void LoadingState::onWindowResize([[maybe_unused]] int newLogicalWidth, [[maybe_unused]] int newLogicalHeight) {
    if (!m_uiInitialized) {
        return;
    }

    // Recreate UI components with new dimensions
    cleanupUI();
    initializeUI();
}

std::string LoadingState::getName() const {
    return "LoadingState";
}

void LoadingState::startAsyncWorldLoad() {
    auto& threadSystem = HammerEngine::ThreadSystem::Instance();
    auto& worldManager = WorldManager::Instance();

    // Capture necessary data by value for thread safety
    auto worldConfig = m_worldConfig;

    // Create lambda that will run on background thread
    auto loadTask = [this, worldConfig, &worldManager]() -> bool {
        GAMESTATE_INFO("Starting async world generation on background thread");

        // Progress callback that updates our atomic progress
        auto progressCallback = [this](float percent, const std::string& status) {
            // Update atomic progress (thread-safe)
            m_progress.store(percent, std::memory_order_release);

            // Update status text (mutex-protected)
            setStatusText(status);
        };

        // Load world with progress callback
        bool success = worldManager.loadNewWorld(worldConfig, progressCallback);

        if (success) {
            GAMESTATE_INFO("Async world generation completed successfully");
        } else {
            std::string errorMsg = "Async world generation failed";
            GAMESTATE_ERROR(errorMsg);

            // Store error for diagnostic purposes
            {
                std::lock_guard<std::mutex> lock(m_errorMutex);
                m_lastError = errorMsg;
            }
        }

        // Mark loading as complete
        m_loadFailed.store(!success, std::memory_order_release);
        m_loadComplete.store(true, std::memory_order_release);

        return success;
    };

    // Enqueue task with high priority and get future
    m_loadTask = threadSystem.enqueueTaskWithResult(
        loadTask,
        HammerEngine::TaskPriority::High,
        "LoadingState_WorldGeneration"
    );
}

void LoadingState::setStatusText(const std::string& status) {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_statusText = status;
}

std::string LoadingState::getStatusText() const {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    return m_statusText;
}

std::string LoadingState::getLastError() const {
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return m_lastError;
}

bool LoadingState::hasError() const {
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return !m_lastError.empty();
}

void LoadingState::initializeUI() {
    auto& ui = UIManager::Instance();
    auto& gameEngine = GameEngine::Instance();
    int windowWidth = gameEngine.getLogicalWidth();
    int windowHeight = gameEngine.getLogicalHeight();

    // Create loading overlay
    ui.createOverlay();

    // Create title
    ui.createTitle("loading_title",
                   {0, windowHeight / 2 - 80, windowWidth, 40},
                   "Loading World...");
    ui.setTitleAlignment("loading_title", UIAlignment::CENTER_CENTER);

    // Create progress bar in center of screen using centered positioning
    int progressBarWidth = 400;
    int progressBarHeight = 30;
    int progressBarY = windowHeight / 2;
    ui.createProgressBar("loading_progress",
                        {0, progressBarY, progressBarWidth, progressBarHeight},
                        0.0f, 100.0f);
    ui.setComponentPositioning("loading_progress",
                              {UIPositionMode::CENTERED_H, 0, progressBarY, progressBarWidth, progressBarHeight});

    // Create status text below progress bar
    ui.createTitle("loading_status",
                   {0, progressBarY + 50, windowWidth, 30},
                   "Initializing...");
    ui.setTitleAlignment("loading_status", UIAlignment::CENTER_CENTER);

    m_uiInitialized = true;

    GAMESTATE_INFO("Loading screen UI initialized");
}

void LoadingState::cleanupUI() {
    if (!m_uiInitialized) {
        return;
    }

    auto& ui = UIManager::Instance();

    ui.removeOverlay();
    ui.removeComponent("loading_title");
    ui.removeComponent("loading_progress");
    ui.removeComponent("loading_status");

    m_uiInitialized = false;

    GAMESTATE_INFO("Loading screen UI cleaned up");
}
