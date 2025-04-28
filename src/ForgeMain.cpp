// Copyright (c) 2025 Hammer Forged Games
// Licensed under the MIT License - see LICENSE file for details
//
#include "GameEngine.hpp"
#include "ThreadSystem.hpp"
#include <SDL3/SDL.h>
#include <iostream>
#include <string>
#include <mutex>
#include <condition_variable>
#include <atomic>

const float FPS{60.0f};
const float DELAY_TIME{1000.0f / FPS};
const int WINDOW_WIDTH{1920};
const int WINDOW_HEIGHT{1080};
//Game Name goes here.
const std::string GAME_NAME{"Game Template"};

//maybe_unused is just a hint to the compiler that the variable is not used. with -Wall -Wextra flags
[[maybe_unused]] int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {

    std::cout << "Forge Game Engine - Initializing " << GAME_NAME << "...\n";
    std::cout << "Forge Game Engine - Initializing Thread System....\n";
   // Initialize the thread system first
  if (!Forge::ThreadSystem::Instance().init()) {
    std::cerr << "Forge Game Engine - Failed to initialize thread system!" << std::endl;
    return -1;
  }

    std::cout << "Forge Game Engine - Thread system initialized with "
              << Forge::ThreadSystem::Instance().getThreadCount() << " worker threads!\n";

    std::mutex updateMutex;
    std::condition_variable updateCondition;
    std::atomic<bool> updateReady{false};

    Uint64 frameStart, frameTime;

    if (GameEngine::Instance().init(GAME_NAME.c_str(), WINDOW_WIDTH, WINDOW_HEIGHT, false)) {
        while (GameEngine::Instance().getRunning()) {
            frameStart = SDL_GetTicks();

            // Handle events on the main thread (this is SDL requirement)
            GameEngine::Instance().handleEvents();

            // Run update in a worker thread
            Forge::ThreadSystem::Instance().enqueueTask([&]() {
                GameEngine::Instance().update();
                {
                    std::lock_guard<std::mutex> lock(updateMutex);
                    updateReady = true;
                }
                updateCondition.notify_one();
            });

            // Process any background tasks while waiting for update
            // This could include asset loading, AI computation, physics, etc.
            Forge::ThreadSystem::Instance().enqueueTask([]() {
                // Example background task
                // GameEngine::Instance().processBackgroundTasks();
            });

            // Wait for update to complete before rendering
            {
                std::unique_lock<std::mutex> lock(updateMutex);
                updateCondition.wait(lock, [&]{ return updateReady.load(); });
            }

            // Render on main thread (OpenGL/SDL rendering context is bound to main thread)
            GameEngine::Instance().render();
            updateReady = false;

            frameTime = SDL_GetTicks() - frameStart;

            if (frameTime < DELAY_TIME) {
                SDL_Delay((int)(DELAY_TIME - frameTime));
            }
        }
    } else {
        std::cerr << "Forge Game Engine - Init " << GAME_NAME << " Failed!:" << SDL_GetError() << std::endl;
        return -1;
    }

    std::cout << "Forge Game Engine - Game " << GAME_NAME << " Shutting down...\n";

    GameEngine::Instance().clean();

    return 0;
}
