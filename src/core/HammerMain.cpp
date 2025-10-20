/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "core/GameEngine.hpp"
#include "core/ThreadSystem.hpp"
#include "core/GameLoop.hpp"
#include "core/Logger.hpp"
#include <string>
#include <string_view>
#include <cstdlib>

const int WINDOW_WIDTH{1920};
const int WINDOW_HEIGHT{1080};
const float TARGET_FPS{60.0f};
const float FIXED_TIMESTEP{1.0f / 60.0f}; // 1:1 with frame rate for responsive input
// Game Name goes here.
const std::string GAME_NAME{"Game Template"};

// maybe_unused is just a hint to the compiler that the variable is not used.
// with -Wall -Wextra flags
int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
  GAMEENGINE_INFO("Initializing " + GAME_NAME);
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
    THREADSYSTEM_CRITICAL("Exception during thread system init: " + std::string(e.what()));
    return -1;
  }

  THREADSYSTEM_INFO("Thread system initialized with " +
                    std::to_string(threadSystem.getThreadCount()) +
                    " worker threads and capacity for " +
                    std::to_string(threadSystem.getQueueCapacity()) +
                    " parallel tasks");

  // Initialize GameEngine
  if (!GameEngine::Instance().init(GAME_NAME, WINDOW_WIDTH, WINDOW_HEIGHT, false)) {
    GAMEENGINE_CRITICAL("Init " + GAME_NAME + " Failed: " + std::string(SDL_GetError()));
    
    // CRITICAL: Always clean up on init failure to prevent memory corruption
    // during static destruction of partially initialized managers
    GAMEENGINE_INFO("Cleaning up after initialization failure");
    GameEngine::Instance().clean();
    
    return -1;
  }

  GAMELOOP_INFO("Initializing Game Loop");

  // Create game loop with stable 60Hz timing
  // Multi-threading enabled for better performance
  auto gameLoop = std::make_shared<GameLoop>(TARGET_FPS, FIXED_TIMESTEP, true);

  // Set GameLoop reference in GameEngine for delegation
  GameEngine::Instance().setGameLoop(gameLoop);

  // Configure TimestepManager for platform-specific frame limiting
  // Wayland: use software frame limiting; Others: prefer hardware VSync
  const char* sessionType = std::getenv("XDG_SESSION_TYPE");
  const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
  const bool isWayland = (sessionType && std::string_view(sessionType) == "wayland") || (waylandDisplay && *waylandDisplay);
  gameLoop->getTimestepManager().setSoftwareFrameLimiting(isWayland);
  if (isWayland) {
    GAMELOOP_INFO("Wayland detected: using software frame limiting (VSync off)");
  } else {
    GAMELOOP_INFO("Non-Wayland: relying on hardware VSync; software limiting off");
  }

  // Cache GameEngine reference for better performance in game loop
  GameEngine& gameEngine = GameEngine::Instance();

  // Register event handler (always on main thread - SDL requirement)
  gameLoop->setEventHandler([&gameEngine]() {
    gameEngine.handleEvents();
  });

  // Register update handler (fixed timestep for consistent game logic)
  gameLoop->setUpdateHandler([&gameEngine](float deltaTime) {
    // Swap buffers if we have a new frame ready for rendering
    if (gameEngine.hasNewFrameToRender()) {
      gameEngine.swapBuffers();
    }

    // Update game logic with fixed timestep
    gameEngine.update(deltaTime);

    // Note: Background tasks removed - processBackgroundTasks() is currently empty
    // and was enqueuing 60 empty tasks/sec, preventing worker threads from going idle.
    // Re-enable if/when actual background work is needed.
  });

  // Register render handler
  gameLoop->setRenderHandler([]() {
    GameEngine::Instance().render();
  });

  GAMELOOP_INFO("Starting Game Loop");

  // Push initial state after GameLoop is fully configured but before starting
  // This ensures the game loop is ready to handle state updates
  GameEngine::Instance().getGameStateManager()->pushState("LogoState");

  // Run the game loop - this blocks until the game ends
  if (!gameLoop->run()) {
    GAMELOOP_CRITICAL("Game loop failed");
    return -1;
  }

  GAMEENGINE_INFO("Game " + GAME_NAME + " shutting down");

  gameEngine.clean();

  return 0;
}
