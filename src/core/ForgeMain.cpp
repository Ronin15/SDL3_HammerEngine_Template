/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include <SDL3/SDL.h>
#include <iostream>
#include <string>
#include "core/GameEngine.hpp"
#include "core/ThreadSystem.hpp"
#include "core/GameLoop.hpp"

const int WINDOW_WIDTH{1920};
const int WINDOW_HEIGHT{1080};
const float TARGET_FPS{60.0f};
const float FIXED_TIMESTEP{1.0f / 75.0f}; // 75Hz fixed update rate for responsive gameplay
// Game Name goes here.
const std::string GAME_NAME{"Game Template"};

// maybe_unused is just a hint to the compiler that the variable is not used.
// with -Wall -Wextra flags
int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
  std::cout << "Forge Game Engine - Initializing " << GAME_NAME << "...\n";
  std::cout << "Forge Game Engine - Initializing Thread System....\n";

  // Initialize the thread system with default capacity
  // Cache ThreadSystem reference for better performance
  Forge::ThreadSystem& threadSystem = Forge::ThreadSystem::Instance();

  // Initialize thread system first
  try {
    if (!threadSystem.init()) {
      std::cerr << "Forge Game Engine - Failed to initialize thread system!"
                << std::endl;
      return -1;
    }
  } catch (const std::exception& e) {
    std::cerr << "Forge Game Engine - Exception during thread system init: " << e.what() << std::endl;
    return -1;
  }

  std::cout << "Forge Game Engine - Thread system initialized with "
            << threadSystem.getThreadCount()
            << " worker threads and capacity for "
            << threadSystem.getQueueCapacity()
            << " parallel tasks!\n";

  // Initialize GameEngine
  if (!GameEngine::Instance().init(GAME_NAME.c_str(), WINDOW_WIDTH, WINDOW_HEIGHT, false)) {
    std::cerr << "Forge Game Engine - Init " << GAME_NAME << " Failed!: " << SDL_GetError() << std::endl;
    return -1;
  }

  std::cout << "Forge Game Engine - Initializing Game Loop...\n";

  // Create game loop with industry-standard timing
  // Multi-threading enabled for better performance
  auto gameLoop = std::make_shared<GameLoop>(TARGET_FPS, FIXED_TIMESTEP, true);

  // Set GameLoop reference in GameEngine for delegation
  GameEngine::Instance().setGameLoop(gameLoop);

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
          std::cerr << "Forge Game Engine - ERROR: Exception in background task: " << e.what() << std::endl;
        }
      });
    } catch (const std::exception& e) {
      std::cerr << "Forge Game Engine - ERROR: Exception enqueuing background task: " << e.what() << std::endl;
    }
  });

  // Register render handler (variable timestep with interpolation)
  gameLoop->setRenderHandler([](float interpolation) {
    GameEngine::Instance().render(interpolation);
  });

  std::cout << "Forge Game Engine - Starting Game Loop...\n";

  // Run the game loop - this blocks until the game ends
  if (!gameLoop->run()) {
    std::cerr << "Forge Game Engine - Game loop failed!" << std::endl;
    return -1;
  }

  std::cout << "Forge Game Engine - Game " << GAME_NAME << " Shutting down...\n";

  gameEngine.clean();

  return 0;
}
