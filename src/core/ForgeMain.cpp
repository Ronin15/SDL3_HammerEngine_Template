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
  try {
    if (!Forge::ThreadSystem::Instance().init()) {
      std::cerr << "Forge Game Engine - Failed to initialize thread system!"
                << std::endl;
      return -1;
    }
  } catch (const std::exception& e) {
    std::cerr << "Forge Game Engine - Exception during thread system initialization: "
              << e.what() << std::endl;
    return -1;
  }

  std::cout << "Forge Game Engine - Thread system initialized with "
            << Forge::ThreadSystem::Instance().getThreadCount()
            << " worker threads and capacity for "
            << Forge::ThreadSystem::Instance().getQueueCapacity()
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

  // Register event handler (always on main thread - SDL requirement)
  gameLoop->setEventHandler([]() {
    GameEngine::Instance().handleEvents();
  });

  // Register update handler (fixed timestep for consistent game logic)
  gameLoop->setUpdateHandler([](float deltaTime) {
    // Swap buffers if we have a new frame ready for rendering
    if (GameEngine::Instance().hasNewFrameToRender()) {
      GameEngine::Instance().swapBuffers();
    }

    // Update game logic with fixed timestep
    GameEngine::Instance().update(deltaTime);

    // Process background tasks using thread system
    try {
      Forge::ThreadSystem::Instance().enqueueTask([]() {
        try {
          GameEngine::Instance().processBackgroundTasks();
        } catch (const std::exception& e) {
          std::cerr << "Forge Game Engine - Exception in background task: " << e.what() << std::endl;
        }
      });
    } catch (const std::exception& e) {
      std::cerr << "Forge Game Engine - Failed to enqueue background task: " << e.what() << std::endl;
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

  GameEngine::Instance().clean();

  return 0;
}
