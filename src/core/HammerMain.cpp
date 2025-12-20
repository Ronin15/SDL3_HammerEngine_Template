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
#include <cstdlib>
#include <format>
#include <string>

const int WINDOW_WIDTH{1280};
const int WINDOW_HEIGHT{720};
// Game Name goes here.
const std::string GAME_NAME{"Game Template"};

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
  uint64_t frameCount = 0;
  static constexpr uint64_t PERF_LOG_INTERVAL = 1800;  // Every 30s @ 60fps
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
      // Swap buffers if we have a new frame ready for rendering
      if (gameEngine.hasNewFrameToRender()) {
        gameEngine.swapBuffers();
      }

      // Update game logic with fixed timestep
      gameEngine.update(ts.getUpdateDeltaTime());
    }

#ifndef NDEBUG
    auto updateEnd = std::chrono::high_resolution_clock::now();
    double updateMs = std::chrono::duration<double, std::milli>(updateEnd - updateStart).count();
    updateSamples[sampleIndex++ % PERF_SAMPLE_COUNT] = updateMs;
    ++frameCount;

    if (frameCount % PERF_LOG_INTERVAL == 0 && !HammerEngine::Logger::IsBenchmarkMode()) {
      double avgMs = 0.0;
      for (double sample : updateSamples) {
        avgMs += sample;
      }
      avgMs /= PERF_SAMPLE_COUNT;
      double frameBudgetMs = 1000.0 / 60.0;  // 16.67ms
      double utilizationPercent = (avgMs / frameBudgetMs) * 100.0;
      GAMEENGINE_DEBUG(std::format("Update performance: {:.2f}ms avg ({:.1f}% frame budget)",
                                   avgMs, utilizationPercent));
    }
#endif

    // Render with interpolation alpha (calculated from remaining accumulator)
    gameEngine.render();

    // End frame (VSync or software frame limiting)
    ts.endFrame();
  }

  GAMEENGINE_INFO(std::format("Game {} shutting down", GAME_NAME));

  gameEngine.clean();

  return 0;
}
