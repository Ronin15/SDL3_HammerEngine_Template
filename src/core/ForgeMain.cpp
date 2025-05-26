/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include <SDL3/SDL.h>
#include <atomic>
#include <iostream>
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

  // Using GameEngine's optimized frame synchronization

  Uint64 frameStart, frameTime;
  static std::atomic<bool> updateInProgress{false};

  if (GameEngine::Instance().init(GAME_NAME.c_str(), WINDOW_WIDTH, WINDOW_HEIGHT, false)) {
    while (GameEngine::Instance().getRunning()) {
      frameStart = SDL_GetTicks();

      // Handle events on the main thread (SDL requirement)
      GameEngine::Instance().handleEvents();

      // Let the ThreadSystem manage its own capacity internally
      // The capacity management is handled internally by the thread system

      // Swap buffers if we have a new frame ready for rendering
      if (GameEngine::Instance().hasNewFrameToRender()) {
        GameEngine::Instance().swapBuffers();
      }

      // Always render from the current render buffer
      // The buffer will only be rendered if it's ready
      GameEngine::Instance().render();

      // Only queue a new update if we're not already updating
      if (!updateInProgress.load(std::memory_order_acquire) && 
          !GameEngine::Instance().isUpdateRunning()) {
        
        // Reset update completion flag for next frame
        GameEngine::Instance().signalUpdateComplete();
        
        // Queue a new update
        try {
          updateInProgress.store(true, std::memory_order_release);
          
          // Copy any needed state - don't capture by reference
          Forge::ThreadSystem::Instance().enqueueTask([]() {
            try {
              GameEngine::Instance().update();
              // Update method internally handles synchronization
            } catch (const std::exception& e) {
              std::cerr << "Forge Game Engine - Exception in update task: " << e.what() << std::endl;
            } catch (...) {
              std::cerr << "Forge Game Engine - Unknown exception in update task" << std::endl;
            }
            
            // Mark update as complete
            updateInProgress.store(false, std::memory_order_release);
          });
        } catch (const std::exception& e) {
          std::cerr << "Forge Game Engine - Failed to enqueue update task: " << e.what() << std::endl;
          updateInProgress.store(false, std::memory_order_release);
        }
      }

      // Process any background tasks in parallel with update
      // This could include asset loading, AI computation, physics, etc.
      try {
        // Copy any needed data rather than capturing by reference
        Forge::ThreadSystem::Instance().enqueueTask([]() {
          try {
            // Example background task
            GameEngine::Instance().processBackgroundTasks();
          } catch (const std::exception& e) {
            std::cerr << "Forge Game Engine - Exception in background task: " << e.what() << std::endl;
          }
        });
      } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Failed to enqueue background task: " << e.what() << std::endl;
      }

      // Calculate frame time
      frameTime = SDL_GetTicks() - frameStart;

      // Limit frame rate to target FPS
      if (frameTime < DELAY_TIME) {
        // We have time to spare, delay to meet target frame rate
        SDL_Delay((int)(DELAY_TIME - frameTime));
      } else if (frameTime > DELAY_TIME * 2) {
        // Frame took too long, might need to skip updates
        std::cerr << "Forge Game Engine - Warning: Frame time exceeds threshold: " 
                  << frameTime << "ms" << std::endl;
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
