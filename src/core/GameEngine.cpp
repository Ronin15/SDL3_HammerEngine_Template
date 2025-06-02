/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "core/GameEngine.hpp"
#include "core/GameLoop.hpp" // IWYU pragma: keep - Required for GameLoop weak_ptr declaration
#include <boost/container/small_vector.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include "SDL3/SDL_surface.h"
#include "managers/AIManager.hpp"
#include "gameStates/AIDemoState.hpp"
#include "gameStates/EventDemoState.hpp"
#include "managers/EventManager.hpp"
#include "managers/FontManager.hpp"
#include "gameStates/GamePlayState.hpp"
#include "managers/GameStateManager.hpp"
#include "managers/InputManager.hpp"
#include "gameStates/LogoState.hpp"
#include "gameStates/MainMenuState.hpp"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_video.h"
#include "managers/SaveGameManager.hpp"
#include "managers/SoundManager.hpp"
#include "core/ThreadSystem.hpp"
#include "managers/TextureManager.hpp"

#define FORGE_GRAY 31, 32, 34, 255

bool GameEngine::init(const char* title,
                      int width,
                      int height,
                      bool fullscreen) {
  std::cout << "Forge Game Engine - Initializing SDL Video....\n";

  if (SDL_Init(SDL_INIT_VIDEO)) {
    std::cout << "Forge Game Engine - SDL Video online!\n";

    // Get display bounds to determine optimal window size
    SDL_Rect display;
    if (SDL_GetDisplayBounds(1, &display) != 0) {  // Try display 1 first
      // Try display 0 as fallback
      if (SDL_GetDisplayBounds(0, &display) != 0) {
        std::cerr
            << "Forge Game Engine - Warning: Could not get display bounds: "
            << SDL_GetError() << std::endl;
        std::cout << "Forge Game Engine - Using default window size: " << width
                  << "x" << height << "\n";
        // Keep the provided dimensions
        m_windowWidth = width;
        m_windowHeight = height;
      } else {
        // Success with display 0
        std::cout
            << "Forge Game Engine - Detected resolution on primary display: "
            << display.w << "x" << display.h << "\n";

        // Continue with display size logic
        if (width <= 0 || height <= 0) {
          m_windowWidth = static_cast<int>(display.w * 0.8f);
          m_windowHeight = static_cast<int>(display.h * 0.8f);
          std::cout << "Forge Game Engine - Adjusted window size to: "
                    << m_windowWidth << "x" << m_windowHeight << "\n";
        } else {
          // Use provided dimensions
          m_windowWidth = width;
          m_windowHeight = height;
        }

        // Set fullscreen if requested dimensions are larger than screen
        if (width > display.w || height > display.h) {
          fullscreen = true;  // true
          std::cout << "Forge Game Engine - Window size larger than screen, "
                       "enabling fullscreen\n";
        }
      }
    } else {
      std::cout << "Forge Game Engine - Detected resolution on display 1: "
                << display.w << "x" << display.h << "\n";

      // Use 80% of display size if no specific size provided
      if (width <= 0 || height <= 0) {
        m_windowWidth = static_cast<int>(display.w * 0.8f);
        m_windowHeight = static_cast<int>(display.h * 0.8f);
        std::cout << "Forge Game Engine - Adjusted window size to: "
                  << m_windowWidth << "x" << m_windowHeight << "\n";
      } else {
        // Use the provided dimensions
        m_windowWidth = width;
        m_windowHeight = height;
        std::cout << "Forge Game Engine - Using requested window size: "
                  << m_windowWidth << "x" << m_windowHeight << "\n";
      }

      // Set fullscreen if requested dimensions are larger than screen
      if (width > display.w || height > display.h) {
        fullscreen = true;  // true
        std::cout << "Forge Game Engine - Window size larger than screen, "
                     "enabling fullscreen\n";
      }
    }
    // Window handling
    int flags{0};
    int flag1 = SDL_WINDOW_FULLSCREEN;
    int flag2 = SDL_WINDOW_HIGH_PIXEL_DENSITY;

    if (fullscreen) {
      flags = flag1 | flag2;
      //setting window width and height to fullscreen dimensions for detected monitor
      m_windowWidth = display.w;
      m_windowHeight = display.h;
      std::cout << "Forge Game Engine - Window size set to Full Screen!\n";
    }

    mp_window.reset(SDL_CreateWindow(title, m_windowWidth, m_windowHeight, flags));

    if (mp_window) {
      std::cout << "Forge Game Engine - Window creation system online!\n";

      // Set window icon
      std::cout << "Forge Game Engine - Setting window icon...\n";

      // Use SDL_image to directly load the icon
      const char* iconPath = "res/img/icon.ico";

      // Use a separate thread to load the icon
      auto iconFuture = Forge::ThreadSystem::Instance().enqueueTaskWithResult(
          [iconPath]() -> std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> {
              return std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(IMG_Load(iconPath), SDL_DestroySurface);
          });

      // Continue with initialization while icon loads
      // account for pixel density for rendering
      int pixelWidth, pixelHeight;
      SDL_GetWindowSizeInPixels(mp_window.get(), &pixelWidth, &pixelHeight);
      // Get logical dimensions
      int logicalWidth, logicalHeight;
      SDL_GetWindowSize(mp_window.get(), &logicalWidth, &logicalHeight);

      mp_renderer.reset(SDL_CreateRenderer(mp_window.get(), NULL));

      if (mp_renderer) {
        std::cout << "Forge Game Engine - Rendering system online!\n";
        SDL_SetRenderDrawColor(mp_renderer.get(), FORGE_GRAY);  // Forge Game Engine gunmetal dark grey
        // Set logical rendering size to match our window size
        SDL_SetRenderLogicalPresentation(mp_renderer.get(), logicalWidth, logicalHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        //Render Mode.
        SDL_SetRenderDrawBlendMode(mp_renderer.get(), SDL_BLENDMODE_BLEND);
      } else {
        std::cerr << "Forge Game Engine - Rendering system creation failed! " << SDL_GetError() << std::endl;
        return false;  // Forge renderer fail
      }

      // Now check if the icon was loaded successfully
      try {
        auto iconSurfacePtr = iconFuture.get();
        if (iconSurfacePtr) {
          SDL_SetWindowIcon(mp_window.get(), iconSurfacePtr.get());
          // No need to manually destroy the surface, smart pointer will handle it
          std::cout << "Forge Game Engine - Window icon set successfully!\n";
        } else {
          std::cerr << "Forge Game Engine - Failed to load window icon: "
                    << SDL_GetError() << std::endl;
        }
      } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Error loading window icon: "
                  << e.what() << std::endl;
      }

    } else {
      std::cerr << "Forge Game Engine - Window system creation failed! "
                << SDL_GetError() << std::endl;
      return false;
    }
  } else {
    std::cerr
        << "Forge Game Engine - SDL Video intialization failed! Make sure you "
           "have the SDL3 runtime installed? SDL error: "
        << SDL_GetError() << std::endl;
    return false;
  }

  // INITIALIZING GAME RESOURCE LOADING AND MANAGEMENT_________________________BEGIN
  // Use multiple threads for initialization
  boost::container::small_vector<std::future<bool>, 8>
      initTasks;  // Store up to 6 tasks without heap allocation

  // Initialize Input Handling in a separate thread - #1
  initTasks.push_back(
      Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
        std::cout << "Forge Game Engine - Detecting and initializing gamepads "
                     "and input handling\n";
        InputManager& inputMgr = InputManager::Instance();
        inputMgr.initializeGamePad();
        return true;
      }));

  // Create and initialize texture manager - MAIN THREAD
  std::cout << "Forge Game Engine - Creating Texture Manager\n";
  TextureManager& texMgr = TextureManager::Instance();
  if (!TextureManager::Exists()) {
    std::cerr << "Forge Game Engine - Failed to create Texture Manager!"<< std::endl;
    return false;
  }

  // Load textures in main thread
  std::cout << "Forge Game Engine - Creating and loading textures\n";
  texMgr.load("res/img", "", mp_renderer.get());

  // Initialize sound manager in a separate thread - #2
  initTasks.push_back(
      Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
        std::cout << "Forge Game Engine - Creating Sound Manager\n";
        SoundManager& soundMgr = SoundManager::Instance();
        if (!soundMgr.init()) {
          std::cerr << "Forge Game Engine - Failed to initialize Sound Manager!" << std::endl;
          return false;
        }

        std::cout << "Forge Game Engine - Loading sounds and music\n";
        soundMgr.loadSFX("res/sfx", "sfx");
        soundMgr.loadMusic("res/music", "music");
        return true;
      }));

  // Initialize font manager in a separate thread - #3
  initTasks.push_back(
      Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
        std::cout << "Forge Game Engine - Creating Font Manager\n";
        FontManager& fontMgr = FontManager::Instance();
        if (!fontMgr.init()) {
          std::cerr << "Forge Game Engine - Failed to initialize Font Manager!"
                    << std::endl;
          return false;
        }
        fontMgr.loadFont("res/fonts", "fonts", 24);
        return true;
      }));

  // Initialize SaveGameManager in a separate thread - #4
  initTasks.push_back(
      Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
        std::cout << "Forge Game Engine - Creating Save Game Manager\n";
        SaveGameManager& saveMgr = SaveGameManager::Instance();
        if (!SaveGameManager::Exists()) {
          std::cerr << "Forge Game Engine - Failed to create Save Game Manager!" << std::endl;
          return false;
        }

        // Set the save directory to "res" folder
        saveMgr.setSaveDirectory("res");
        return true;
      }));

  // Initialize AI Manager in a separate thread - #5
  initTasks.push_back(
      Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
        std::cout << "Forge Game Engine - Creating AI Manager\n";
        AIManager& aiMgr = AIManager::Instance();
        if (!aiMgr.init()) {
          std::cerr << "Forge Game Engine - Failed to initialize AI Manager!" << std::endl;
          return false;
        }
        std::cout << "Forge Game Engine - AI Manager initialized successfully\n";
        return true;
      }));

  // Initialize Event Manager in a separate thread - #6
  initTasks.push_back(
      Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
        std::cout << "Forge Game Engine - Creating Event Manager\n";
        EventManager& eventMgr = EventManager::Instance();
        if (!eventMgr.init()) {
          std::cerr << "Forge Game Engine - Failed to initialize Event Manager!" << std::endl;
          return false;
        }
        std::cout << "Forge Game Engine - Event Manager initialized successfully\n";
        return true;
      }));

  // Initialize game state manager (on main thread because it directly calls rendering) - MAIN THREAD
  std::cout << "Forge Game Engine - Creating Game State Manager and setting up "
               "initial Game States\n";
  mp_gameStateManager = std::make_unique<GameStateManager>();

  // Setting Up initial game states
  mp_gameStateManager->addState(std::make_unique<LogoState>());
  mp_gameStateManager->addState(std::make_unique<MainMenuState>());
  mp_gameStateManager->addState(std::make_unique<GamePlayState>());
  mp_gameStateManager->addState(std::make_unique<AIDemoState>());
  mp_gameStateManager->addState(std::make_unique<EventDemoState>());

  // Wait for all initialization tasks to complete
  bool allTasksSucceeded = true;
  for (auto& task : initTasks) {
    try {
      allTasksSucceeded &= task.get();
    } catch (const std::exception& e) {
      std::cerr << "Forge Game Engine - Initialization task failed: "
                << e.what() << std::endl;
      allTasksSucceeded = false;
    }
  }

  if (!allTasksSucceeded) {
    std::cerr << "Forge Game Engine - One or more initialization tasks failed"
              << std::endl;
    return false;
  }
  //_______________________________________________________________________________________________________________END

  std::cout << "Forge Game Engine - Game " << title << " initialized successfully!\n";
  std::cout << "Forge Game Engine - Running " << title << " <]==={}\n";

  // setting logo state for default state
  mp_gameStateManager->setState("LogoState");//set to "LogoState" for normal operation.
  // Note: GameLoop will be started by ForgeMain, not here
  return true;
}

void GameEngine::handleEvents() {
  // Cache InputManager reference for better performance
  InputManager& inputMgr = InputManager::Instance();
  inputMgr.update();
}

void GameEngine::setRunning(bool running) {
  if (auto gameLoop = m_gameLoop.lock()) {
    if (running) {
      // Can't restart GameLoop from GameEngine - this might be an error case
      std::cerr << "Warning: Cannot start GameLoop from GameEngine - use GameLoop::run() instead" << std::endl;
    } else {
      gameLoop->stop();
    }
  } else {
    std::cerr << "Warning: No GameLoop set - cannot change running state" << std::endl;
  }
}

bool GameEngine::getRunning() const {
  if (auto gameLoop = m_gameLoop.lock()) {
    return gameLoop->isRunning();
  }
  return false;
}

float GameEngine::getCurrentFPS() const {
  if (auto gameLoop = m_gameLoop.lock()) {
    return gameLoop->getCurrentFPS();
  }
  return 0.0f;
}

void GameEngine::update([[maybe_unused]] float deltaTime) {
  // This method is now thread-safe and can be called from a worker thread
  std::lock_guard<std::mutex> lock(m_updateMutex);

  // Mark update as running
  m_updateRunning = true;

  // Get the buffer for the current update
  const size_t updateBufferIndex = m_currentBufferIndex.load(std::memory_order_acquire);

  try {
    // Update game states with fixed timestep - make sure GameStateManager knows which buffer to update
    mp_gameStateManager->update(deltaTime);

    // Increment the frame counter
    m_lastUpdateFrame++;

    // Mark this buffer as ready for rendering
    m_bufferReady[updateBufferIndex].store(true, std::memory_order_release);

    // Mark update as completed
    m_updateCompleted = true;
  } catch (const std::exception& e) {
    std::cerr << "Forge Game Engine - Exception in update: " << e.what() << std::endl;
  } catch (...) {
    std::cerr << "Forge Game Engine - Unknown exception in update" << std::endl;
  }

  m_updateRunning = false;

  // Notify anyone waiting on this update
  m_updateCondition.notify_all();
  m_bufferCondition.notify_all();
}

void GameEngine::render([[maybe_unused]] float interpolation) {
  // Always on MAIN thread as its an - SDL REQUIREMENT
  std::lock_guard<std::mutex> lock(m_renderMutex);

  // Get the current render buffer index
  const size_t renderBufferIndex = m_renderBufferIndex.load(std::memory_order_acquire);

  // Only render if the buffer is ready
  if (m_bufferReady[renderBufferIndex].load(std::memory_order_acquire)) {
    try {
      SDL_RenderClear(mp_renderer.get());

      // Make sure GameStateManager knows which buffer to render from
      mp_gameStateManager->render();

      SDL_RenderPresent(mp_renderer.get());

      // Update the last rendered frame counter with memory ordering
      m_lastRenderedFrame.store(m_lastUpdateFrame.load(std::memory_order_acquire),
                                std::memory_order_release);
    } catch (const std::exception& e) {
      std::cerr << "Forge Game Engine - Exception in render: " << e.what() << std::endl;
    } catch (...) {
      std::cerr << "Forge Game Engine - Unknown exception in render" << std::endl;
    }
  }
}

void GameEngine::waitForUpdate() {
  std::unique_lock<std::mutex> lock(m_updateMutex);
  // Set a timeout to avoid deadlocks with extremely high entity counts
  if (!m_updateCondition.wait_for(lock, std::chrono::milliseconds(100),
      [this] { return m_updateCompleted.load(std::memory_order_acquire); })) {
    // If timeout occurs, log a warning but continue
    std::cerr << "Forge Game Engine - Warning: Update wait timeout with high entity count" << std::endl;
    // Force the update completed flag to true to avoid deadlock
    m_updateCompleted.store(true, std::memory_order_release);
  }
}

void GameEngine::signalUpdateComplete() {
  {
    std::lock_guard<std::mutex> lock(m_updateMutex);
    m_updateCompleted.store(false, std::memory_order_release);  // Reset for next frame
  }
}

void GameEngine::swapBuffers() {
  std::lock_guard<std::mutex> lock(m_bufferMutex);

  // Get current index
  size_t currentIndex = m_currentBufferIndex.load(std::memory_order_acquire);

  // Calculate next buffer index
  size_t nextUpdateIndex = (currentIndex + 1) % BUFFER_COUNT;

  // Only swap if the current update buffer is ready
  if (m_bufferReady[currentIndex].load(std::memory_order_acquire)) {
    // Update buffer is ready, make it the render buffer
    m_renderBufferIndex.store(currentIndex, std::memory_order_release);

    // Mark the next update buffer as not ready
    m_bufferReady[nextUpdateIndex].store(false, std::memory_order_release);

    // Switch to the next buffer for updates
    m_currentBufferIndex.store(nextUpdateIndex, std::memory_order_release);
  }
}

bool GameEngine::hasNewFrameToRender() const {
  // We have a new frame to render if any buffer is ready that isn't the current render buffer
  size_t renderIndex = m_renderBufferIndex.load(std::memory_order_acquire);

  // Check if there's a buffer ready for rendering
  for (size_t i = 0; i < BUFFER_COUNT; ++i) {
    if (i != renderIndex && m_bufferReady[i].load(std::memory_order_acquire)) {
      return true;
    }
  }

  // Also check frame counters as a fallback
  uint64_t lastUpdate = m_lastUpdateFrame.load(std::memory_order_acquire);
  uint64_t lastRendered = m_lastRenderedFrame.load(std::memory_order_acquire);
  return lastUpdate > lastRendered;
}

bool GameEngine::isUpdateRunning() const {
  return m_updateRunning.load(std::memory_order_acquire);
}

size_t GameEngine::getCurrentBufferIndex() const {
  return m_currentBufferIndex.load(std::memory_order_acquire);
}

size_t GameEngine::getRenderBufferIndex() const {
  return m_renderBufferIndex.load(std::memory_order_acquire);
}

void GameEngine::processBackgroundTasks() {
  // This method can be used to perform background processing
  // It should be safe to run on worker threads

  try {
    // Cache EventManager reference for better performance
    EventManager& eventMgr = EventManager::Instance();
    
    // Update Event Manager
    eventMgr.update();
  } catch (const std::exception& e) {
    std::cerr << "Forge Game Engine - Exception in background tasks: " << e.what() << std::endl;
  } catch (...) {
    std::cerr << "Forge Game Engine - Unknown exception in background tasks" << std::endl;
  }

  // Example: Process AI, physics, or other non-rendering tasks
  // These tasks can run while the main thread is handling rendering
}

void GameEngine::clean() {
  std::cout << "Forge Game Engine - Starting shutdown sequence...\n";

  // Cache manager references for better performance
  Forge::ThreadSystem& threadSystem = Forge::ThreadSystem::Instance();
  FontManager& fontMgr = FontManager::Instance();
  SoundManager& soundMgr = SoundManager::Instance();
  EventManager& eventMgr = EventManager::Instance();
  AIManager& aiMgr = AIManager::Instance();
  SaveGameManager& saveMgr = SaveGameManager::Instance();
  InputManager& inputMgr = InputManager::Instance();
  TextureManager& texMgr = TextureManager::Instance();

  // Wait for any pending background tasks to complete
  if (!threadSystem.isShutdown()) {
    std::cout
        << "Forge Game Engine - Waiting for background tasks to complete...\n";
    while (threadSystem.isBusy()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Short delay to avoid busy waiting
    }
  }

  // Clean up engine managers (non-singletons)
  std::cout << "Forge Game Engine - Cleaning up GameState manager...\n";
  mp_gameStateManager.reset();

  // Save copies of the smart pointers to resources we'll clean up at the very end
  auto window_to_destroy = std::move(mp_window);
  auto renderer_to_destroy = std::move(mp_renderer);

  // Clean up Managers in reverse order of their initialization
  std::cout << "Forge Game Engine - Cleaning up Font Manager...\n";
  fontMgr.clean();

  std::cout << "Forge Game Engine - Cleaning up Sound Manager...\n";
  soundMgr.clean();

  std::cout << "Forge Game Engine - Cleaning up Event Manager...\n";
  eventMgr.clean();

  std::cout << "Forge Game Engine - Cleaning up AI Manager...\n";
  aiMgr.clean();

  std::cout << "Forge Game Engine - Cleaning up Save Game Manager...\n";
  saveMgr.clean();

  std::cout << "Forge Game Engine - Cleaning up Input Manager...\n";
  inputMgr.clean();

  std::cout << "Forge Game Engine - Cleaning up Texture Manager...\n";
  texMgr.clean();

  // Clean up the thread system
  std::cout << "Forge Game Engine - Cleaning up Thread System...\n";
  if (!threadSystem.isShutdown()) {
    threadSystem.clean();
  }

  // Finally clean up SDL resources
  std::cout << "Forge Game Engine - Cleaning up SDL resources...\n";

  // Explicitly reset smart pointers at the end, after all subsystems
  // are done using them - this will trigger their custom deleters
  renderer_to_destroy.reset();
  window_to_destroy.reset();
  SDL_Quit();
  std::cout << "Forge Game Engine - SDL resources cleaned!\n";
  std::cout << "Forge Game Engine - Shutdown complete!\n";
}
