// Copyright (c) 2025 Hammer Forged Games
// Licensed under the MIT License - see LICENSE file for details
//
#include "GameEngine.hpp"
#include <SDL3/SDL.h>
#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <functional>

const float FPS{60.0f};
const float DELAY_TIME{1000.0f / FPS};
const int WINDOW_WIDTH{1920};
const int WINDOW_HEIGHT{1080};
//Game Name goes here.
const std::string GAME_NAME{"Game Template"};

// Thread-safe task queue for the worker thread pool
class TaskQueue {
private:
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stopping{false};

public:
    void push(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            tasks.push(std::move(task));
        }
        condition.notify_one();
    }

    bool pop(std::function<void()>& task) {
        std::unique_lock<std::mutex> lock(queueMutex);
        condition.wait(lock, [this] { return stopping || !tasks.empty(); });

        if (stopping && tasks.empty()) {
            return false;
        }

        task = std::move(tasks.front());
        tasks.pop();
        return true;
    }

    void stop() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stopping = true;
        }
        condition.notify_all();
    }

    bool isEmpty() {
        std::unique_lock<std::mutex> lock(queueMutex);
        return tasks.empty();
    }
};

// Thread pool for managing worker threads
class ThreadPool {
private:
    std::vector<std::thread> workers;
    TaskQueue taskQueue;
    std::atomic<bool> isRunning{true};

    void workerThread() {
        std::function<void()> task;
        while (isRunning) {
            if (taskQueue.pop(task)) {
                task();
            }
        }
    }

public:
    ThreadPool(size_t numThreads) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] { workerThread(); });
        }
    }

    ~ThreadPool() {
        isRunning = false;
        taskQueue.stop();

        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void enqueue(std::function<void()> task) {
        taskQueue.push(std::move(task));
    }

    bool busy() {
        return !taskQueue.isEmpty();
    }
};

//maybe_unused is just a hint to the compiler that the variable is not used. with -Wall -Wextra flags
[[maybe_unused]] int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    // Determine the optimal number of threads (leave one core for OS/other tasks)
    unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
    std::cout << "Forge Game Engine - Using " << numThreads << " worker threads\n";

    // Create thread pool
    ThreadPool threadPool(numThreads);

    // Synchronization primitives for update and render threads
    std::mutex updateMutex;
    std::condition_variable updateCondition;
    std::atomic<bool> updateReady{false};
    std::atomic<bool> renderReady{false};

    Uint64 frameStart, frameTime;

    std::cout << "Forge Game Engine - Initializing " << GAME_NAME << "...\n";

    if (GameEngine::Instance().init(GAME_NAME.c_str(), WINDOW_WIDTH, WINDOW_HEIGHT, false)) {
        while (GameEngine::Instance().getRunning()) {
            frameStart = SDL_GetTicks();

            // Handle events on the main thread (this is SDL requirement)
            GameEngine::Instance().handleEvents();

            // Run update in a worker thread
            threadPool.enqueue([&]() {
                GameEngine::Instance().update();
                {
                    std::lock_guard<std::mutex> lock(updateMutex);
                    updateReady = true;
                }
                updateCondition.notify_one();
            });

            // Process any background tasks while waiting for update
            // This could include asset loading, AI computation, physics, etc.
            threadPool.enqueue([]() {
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
        std::cerr << "Forge Game Engine - Init " << GAME_NAME << " Failed!:" << SDL_GetError();
        return -1;
    }

    std::cout << "Forge Game Engine - Game " << GAME_NAME << " Shutting down...\n";

    GameEngine::Instance().clean();

    return 0;
}
