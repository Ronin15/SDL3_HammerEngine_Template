/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include <string>
#include <string_view>
#include <cstdlib>
#include "core/GameEngine.hpp"
#include "core/ThreadSystem.hpp"
#include "core/GameLoop.hpp"
#include "core/Logger.hpp"

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
  if (GameEngine::Instance().isWayland()) {
    gameLoop->getTimestepManager().setSoftwareFrameLimiting(true);
    GAMELOOP_INFO("Configured TimestepManager for Wayland software frame limiting");
  } else {
    gameLoop->getTimestepManager().setSoftwareFrameLimiting(false);
    GAMELOOP_INFO("Configured TimestepManager for hardware VSync");
  }

  // Cache GameEngine reference for better performance in game loop
  GameEngine& gameEngine = GameEngine::Instance();

  // Register event handler (always on main thread - SDL requirement)
  gameLoop->setEventHandler([&gameEngine]() {
    gameEngine.handleEvents();
  });

  // Register update handler (fixed timestep for consistent game logic)
  gameLoop->setUpdateHandler([&gameEngine, &threadSystem](float deltaTime) {
    // Swap buffers if we have a new frame ready for rendering
    if (gameEngine.hasNewFrameToRender()) {
      gameEngine.swapBuffers();
    }

    // Update game logic with fixed timestep
    gameEngine.update(deltaTime);

    // Process background tasks using thread system
    try {
      threadSystem.enqueueTask([&gameEngine]() {
        try {
          gameEngine.processBackgroundTasks();
        } catch (const std::exception& e) {
          GAMEENGINE_ERROR("Exception in background task: " + std::string(e.what()));
        }
      });
    } catch (const std::exception& e) {
      GAMEENGINE_ERROR("Exception enqueuing background task: " + std::string(e.what()));
    }
  });

  // Register render handler
  gameLoop->setRenderHandler([](double alpha) {
    GameEngine::Instance().render(alpha);
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
