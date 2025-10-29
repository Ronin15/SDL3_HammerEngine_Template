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
#include <random>

void LoadingState::configure(const std::string& targetStateName,
                             const HammerEngine::WorldGenerationConfig& worldConfig) {
    m_targetStateName = targetStateName;
    m_worldConfig = worldConfig;

    // Reset state for reuse
    m_progress.store(0.0f, std::memory_order_release);
    m_loadComplete.store(false, std::memory_order_release);
    m_loadFailed.store(false, std::memory_order_release);
    setStatusText("Initializing...");
}

bool LoadingState::enter() {
    GAMESTATE_INFO("Entering LoadingState - Target: " + m_targetStateName);

    // Initialize loading screen UI
    initializeUI();

    // Start async world loading
    startAsyncWorldLoad();

    return true;
}

void LoadingState::update([[maybe_unused]] float deltaTime) {
    // Check if loading is complete
    if (m_loadComplete.load(std::memory_order_acquire)) {
        if (m_loadFailed.load(std::memory_order_acquire)) {
            GAMESTATE_ERROR("World loading failed - transitioning anyway");
            // Continue to target state even on failure (matches current behavior)
        } else {
            GAMESTATE_INFO("World loading complete - transitioning to " + m_targetStateName);
        }

        // Transition to target state
        const auto& gameEngine = GameEngine::Instance();
        auto* gameStateManager = gameEngine.getGameStateManager();
        if (gameStateManager && gameStateManager->hasState(m_targetStateName)) {
            gameStateManager->changeState(m_targetStateName);
        } else {
            GAMESTATE_ERROR("Target state not found: " + m_targetStateName);
        }
    }
}

void LoadingState::render() {
    // All rendering happens through GameEngine::render() -> this method
    // No manual SDL_RenderClear() or SDL_RenderPresent() calls needed!

    auto& ui = UIManager::Instance();

    // Update progress bar
    float currentProgress = m_progress.load(std::memory_order_acquire);
    ui.updateProgressBar("loading_progress", currentProgress);

    // Update status text
    ui.setText("loading_status", getStatusText());

    // Actually render the UI to the screen!
    ui.render();
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
            GAMESTATE_ERROR("Exception while waiting for load task: " + std::string(e.what()));
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
            GAMESTATE_ERROR("Async world generation failed");
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

    // Create progress bar in center of screen
    int progressBarWidth = 400;
    int progressBarHeight = 30;
    int progressBarX = (windowWidth - progressBarWidth) / 2;
    int progressBarY = windowHeight / 2;
    ui.createProgressBar("loading_progress",
                        {progressBarX, progressBarY, progressBarWidth, progressBarHeight},
                        0.0f, 100.0f);

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
