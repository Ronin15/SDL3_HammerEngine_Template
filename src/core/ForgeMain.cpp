/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include <SDL3/SDL.h>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include "core/GameEngine.hpp"
#include "core/ThreadSystem.hpp"

const float FPS{60.0f};
const float DELAY_TIME{1000.0f / FPS};
const int WINDOW_WIDTH{1920};
const int WINDOW_HEIGHT{1080};
// Game Name goes here.
const std::string GAME_NAME{"Game Template"};

// maybe_unused is just a hint to the compiler that the variable is not used.
// with -Wall -Wextra flags
[[maybe_unused]] int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
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

  std::mutex updateMutex;
  std::condition_variable updateCondition;
  std::atomic<bool> updateReady{false};

  Uint64 frameStart, frameTime;

  if (GameEngine::Instance().init(GAME_NAME.c_str(), WINDOW_WIDTH, WINDOW_HEIGHT, false)) {
    while (GameEngine::Instance().getRunning()) {
      frameStart = SDL_GetTicks();

      // Handle events on the main thread (SDL requirement)
      GameEngine::Instance().handleEvents();

      // Let the ThreadSystem manage its own capacity internally
      // The capacity management is handled internally by the thread system

      // Run update in a worker thread
      try {
        Forge::ThreadSystem::Instance().enqueueTask([&]() {
          try {
            GameEngine::Instance().update();
            {
              std::lock_guard<std::mutex> lock(updateMutex);
              updateReady = true;
            }
            updateCondition.notify_one();
          } catch (const std::exception& e) {
            std::cerr << "Forge Game Engine - Exception in update task: " << e.what() << std::endl;
            // Still signal completion even if there was an error
            std::lock_guard<std::mutex> lock(updateMutex);
            updateReady = true;
            updateCondition.notify_one();
          }
        });
      } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Failed to enqueue update task: " << e.what() << std::endl;
        // If we can't enqueue the task, mark update as ready to avoid deadlock
        std::lock_guard<std::mutex> lock(updateMutex);
        updateReady = true;
        updateCondition.notify_one();
      }

      // Process any background tasks when waiting on update
      // This could include asset loading, AI computation, physics, etc.
      try {
        Forge::ThreadSystem::Instance().enqueueTask([]() {
          try {
            // Example background task
            // GameEngine::Instance().processBackgroundTasks();
          } catch (const std::exception& e) {
            std::cerr << "Forge Game Engine - Exception in background task: " << e.what() << std::endl;
          }
        });
      } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Failed to enqueue background task: " << e.what() << std::endl;
      }

      // Wait on update to complete before rendering
      {
        std::unique_lock<std::mutex> lock(updateMutex);
        updateCondition.wait(lock, [&] { return updateReady.load(); });
      }

      // Render on main thread (OpenGL/SDL rendering context is bound to main
      // thread)
      GameEngine::Instance().render();
      updateReady = false;

      frameTime = SDL_GetTicks() - frameStart;

      if (frameTime < DELAY_TIME) {
        SDL_Delay((int)(DELAY_TIME - frameTime));
      }
    }
  } else {
    std::cerr << "Forge Game Engine - Init " << GAME_NAME << " Failed!: " << SDL_GetError() << std::endl;
    return -1;
  }

  std::cout << "Forge Game Engine - Game " << GAME_NAME << " Shutting down...\n";

  GameEngine::Instance().clean();

  return 0;
}
