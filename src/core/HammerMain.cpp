/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "core/GameEngine.hpp"
#include "core/ThreadSystem.hpp"
#include "core/TimestepManager.hpp"
#include "core/Logger.hpp"
#include "managers/SettingsManager.hpp"
#include <array>
#include <chrono>
#include <format>
#include <numeric>
#include <string_view>

constexpr int WINDOW_WIDTH{1280};
constexpr int WINDOW_HEIGHT{720};
constexpr std::string_view GAME_NAME{"Game Template"};

// maybe_unused is just a hint to the compiler that the variable is not used.
// with -Wall -Wextra flags
int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
  GAMEENGINE_INFO(std::format("Initializing {}", GAME_NAME));
  THREADSYSTEM_INFO("Initializing Thread System");

  // Initialize the thread system with default capacity
  // Cache ThreadSystem reference for better performance
  HammerEngine::ThreadSystem& threadSystem = HammerEngine::ThreadSystem::Instance();

  // Initialize thread system first
  try {
    if (!threadSystem.init()) {
      THREADSYSTEM_CRITICAL("Failed to initialize thread system");
      return -1;
    }
  } catch (const std::exception& e) {
    THREADSYSTEM_CRITICAL(std::format("Exception during thread system init: {}", e.what()));
    return -1;
  }

  THREADSYSTEM_INFO(std::format("Thread system initialized with {} worker threads and capacity for {} parallel tasks",
                                threadSystem.getThreadCount(), threadSystem.getQueueCapacity()));

  // Load settings from disk before GameEngine initialization
  // This ensures VSync and other settings are loaded before they're applied
  auto& settingsManager = HammerEngine::SettingsManager::Instance();
  if (!settingsManager.loadFromFile("res/settings.json")) {
    GAMEENGINE_WARN("Failed to load settings.json - using defaults");
  } else {
    GAMEENGINE_INFO("Settings loaded from res/settings.json");
  }

  // Read graphics settings from SettingsManager
  const int windowWidth = settingsManager.get<int>("graphics", "resolution_width", WINDOW_WIDTH);
  const int windowHeight = settingsManager.get<int>("graphics", "resolution_height", WINDOW_HEIGHT);
  const bool fullscreen = settingsManager.get<bool>("graphics", "fullscreen", false);

  // Cache GameEngine reference
  GameEngine& gameEngine = GameEngine::Instance();

  // Initialize GameEngine (creates TimestepManager internally)
  if (!gameEngine.init(GAME_NAME, windowWidth, windowHeight, fullscreen)) {
    GAMEENGINE_CRITICAL(std::format("Init {} Failed: {}", GAME_NAME, SDL_GetError()));

    // CRITICAL: Always clean up on init failure to prevent memory corruption
    // during static destruction of partially initialized managers
    GAMEENGINE_INFO("Cleaning up after initialization failure");
    gameEngine.clean();

    return -1;
  }

  GAMEENGINE_INFO(std::format("Frame timing configured: {}",
                              gameEngine.isUsingSoftwareFrameLimiting()
                              ? "software frame limiting"
                              : "hardware VSync"));

  // Push initial state before starting main loop
  gameEngine.getGameStateManager()->pushState("LogoState");

  GAMEENGINE_INFO("Starting Main Loop");

  // Get TimestepManager reference for main loop
  TimestepManager& ts = gameEngine.getTimestepManager();

#ifndef NDEBUG
  // Update performance tracking (DEBUG only)
  static constexpr size_t PERF_SAMPLE_COUNT = 10;
  std::array<double, PERF_SAMPLE_COUNT> updateSamples{};
  size_t sampleIndex = 0;
  auto lastPerfLogTime = std::chrono::high_resolution_clock::now();
#endif

  // Main game loop - classic fixed timestep pattern
  // Updates drain accumulator, THEN render reads alpha - no race conditions
  while (gameEngine.isRunning()) {
    // Start frame timing (adds delta to accumulator)
    ts.startFrame();

    // Process SDL events (must be on main thread)
    gameEngine.handleEvents();

    // Fixed timestep updates - run until accumulator is drained
#ifndef NDEBUG
    auto updateStart = std::chrono::high_resolution_clock::now();
#endif

    while (ts.shouldUpdate()) {
      gameEngine.update(ts.getUpdateDeltaTime());
    }

#ifndef NDEBUG
    auto updateEnd = std::chrono::high_resolution_clock::now();
    double updateMs = std::chrono::duration<double, std::milli>(updateEnd - updateStart).count();
    updateSamples[sampleIndex++ % PERF_SAMPLE_COUNT] = updateMs;

    double secondsSinceLastLog = std::chrono::duration<double>(updateEnd - lastPerfLogTime).count();
    if (secondsSinceLastLog >= TimestepManager::PERF_LOG_INTERVAL_SECONDS) {
      lastPerfLogTime = updateEnd;
      double avgMs = std::accumulate(updateSamples.begin(), updateSamples.end(), 0.0) / PERF_SAMPLE_COUNT;
      double frameBudgetMs = 1000.0 / ts.getTargetFPS();
      double utilizationPercent = (avgMs / frameBudgetMs) * 100.0;
      GAMEENGINE_DEBUG(std::format("Update performance: {:.2f}ms avg ({:.1f}% frame budget)",
                                   avgMs, utilizationPercent));
    }
#endif

    // Render with interpolation alpha (calculated from remaining accumulator)
    gameEngine.render();

    // Process background tasks during vsync wait (non-critical, async work)
    gameEngine.processBackgroundTasks();

    // End frame (VSync or software frame limiting)
    ts.endFrame();
  }

  GAMEENGINE_INFO(std::format("Game {} shutting down", GAME_NAME));

  gameEngine.clean();

  return 0;
}
