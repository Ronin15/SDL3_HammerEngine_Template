// Copyright (c) 2025 Hammer Forged Games
// Licensed under the MIT License - see LICENSE file for details

#include "GameEngine.hpp"
#include "GamePlayState.hpp"
#include "GameStateManager.hpp"
#include "FontManager.hpp"
#include "InputHandler.hpp"
#include "LogoState.hpp"
#include "MainMenuState.hpp"
#include "SoundManager.hpp"
#include "ThreadSystem.hpp"
#include <iostream>
#include <future>
#include <boost/container/small_vector.hpp>

#define FORGE_GRAY 31, 32, 34, 255

bool GameEngine::init(const char* title, int width, int height, bool fullscreen) {

  std::cout << "Forge Game Engine - Initializing SDL Video....\n";

  if (SDL_Init(SDL_INIT_VIDEO)) {
    std::cout << "Forge Game Engine - SDL Video online!\n";

    // Get display bounds to determine optimal window size
    SDL_Rect display;
    if (SDL_GetDisplayBounds(1, &display) != 0) { // Try display 1 first
      // Try display 0 as fallback
      if (SDL_GetDisplayBounds(0, &display) != 0) {
        std::cerr << "Forge Game Engine - Warning: Could not get display bounds: " << SDL_GetError() << std::endl;
        std::cout << "Forge Game Engine - Using default window size: " << width << "x" << height << "\n";
        // Keep the provided dimensions
        m_windowWidth = width;
        m_windowHeight = height;
      } else {
        // Success with display 0
        std::cout << "Forge Game Engine - Detected resolution on primary display: " << display.w << "x" << display.h << "\n";

        // Continue with display size logic
        if (width <= 0 || height <= 0) {
          m_windowWidth = static_cast<int>(display.w * 0.8f);
          m_windowHeight = static_cast<int>(display.h * 0.8f);
          std::cout << "Forge Game Engine - Adjusted window size to: " << m_windowWidth << "x" << m_windowHeight << "\n";
        } else {
          // Use provided dimensions
          m_windowWidth = width;
          m_windowHeight = height;
        }

        // Set fullscreen if requested dimensions are larger than screen
        if (width > display.w || height > display.h) {
          fullscreen = true; //true
          std::cout << "Forge Game Engine - Window size larger than screen, enabling fullscreen\n";
        }
      }
    } else {
      std::cout << "Forge Game Engine - Detected resolution on display 1: " << display.w << "x" << display.h << "\n";

      // Use 80% of display size if no specific size provided
      if (width <= 0 || height <= 0) {
        m_windowWidth = static_cast<int>(display.w * 0.8f);
        m_windowHeight = static_cast<int>(display.h * 0.8f);
        std::cout << "Forge Game Engine - Adjusted window size to: " << m_windowWidth << "x" << m_windowHeight << "\n";
      } else {
        // Use the provided dimensions
        m_windowWidth = width;
        m_windowHeight = height;
        std::cout << "Forge Game Engine - Using requested window size: " << m_windowWidth << "x" << m_windowHeight << "\n";
      }

      // Set fullscreen if requested dimensions are larger than screen
      if (width > display.w || height > display.h) {
        fullscreen = true; //true
        std::cout << "Forge Game Engine - Window size larger than screen, enabling fullscreen\n";
      }
    }
    // Fullscreen handling
    int flags{0};

    if (fullscreen) {
      flags = SDL_WINDOW_FULLSCREEN;
      std::cout << "Forge Game Engine - Window size set to Full Screen!\n";
    }

    mp_window = SDL_CreateWindow(title, m_windowWidth, m_windowHeight, flags);

    if (mp_window) {
      std::cout << "Forge Game Engine - Window creation system online!\n";

      // Set window icon
      std::cout << "Forge Game Engine - Setting window icon...\n";

      // Use SDL_image to directly load the icon
      // Multiple paths are tried to ensure the icon can be found regardless of current directory
      SDL_Surface* iconSurface = nullptr;
      const char* iconPath = "res/img/icon.ico";

      // Use a separate thread to load the icon
      auto iconFuture = Forge::ThreadSystem::Instance().enqueueTaskWithResult([iconPath]() -> SDL_Surface* {
          return IMG_Load(iconPath);
      });

      // Continue with initialization while icon loads
      mp_renderer = SDL_CreateRenderer(mp_window, NULL);

      if (mp_renderer) {
        std::cout << "Forge Game Engine - Rendering system online!\n";
        SDL_SetRenderDrawColor(mp_renderer, FORGE_GRAY);  // Forge Game Engine gunmetal dark grey
      } else {
        std::cerr << "Forge Game Engine - Rendering system creation failed! "
                  << SDL_GetError() << std::endl;
        return false;  // Forge renderer fail
      }

      // Check if the icon loaded successfully
      try {
        iconSurface = iconFuture.get();
        if (iconSurface) {
          SDL_SetWindowIcon(mp_window, iconSurface);
          SDL_DestroySurface(iconSurface);
          std::cout << "Forge Game Engine - Window icon set successfully!\n";
        } else {
          std::cerr << "Forge Game Engine - Failed to load window icon: " << SDL_GetError() << std::endl;
        }
      } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Error loading icon: " << e.what() << std::endl;
      }

    } else {
      std::cerr << "Forge Game Engine- Window system creation failed! " << SDL_GetError() << std::endl;
      return false;
    }
  } else {
    std::cerr << "Forge Game Engine - SDL Video intialization failed! Make sure you "
                 "have the SDL3 runtime installed? SDL error: "
              << SDL_GetError() << std::endl;
    return false;
  }

  //INITIALIZING GAME RESOURCE LOADING AND MANAGEMENT_________________________________________________________________________________BEGIN

  // Use multiple threads for initialization
  boost::container::small_vector<std::future<bool>, 4> initTasks; // Store up to 4 tasks without heap allocation

  // Initialize Input Handling in a separate thread
  initTasks.push_back(Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
    std::cout << "Forge Game Engine - Detecting and initializing gamepads and input handling\n";
    InputHandler::Instance().initializeGamePad();
    return true;
  }));

  // Create and initialize texture manager
  std::cout << "Forge Game Engine - Creating Texture Manager\n";
  mp_textureManager = new TextureManager();
  if (!mp_textureManager) {
    std::cerr << "Forge Game Engine - Failed to create Texture Manager!" << std::endl;
    return false;
  }

  // Load textures in main thread
  std::cout << "Forge Game Engine - Creating and loading textures\n";
    TextureManager::Instance().load("res/img", "", mp_renderer);

  // Initialize sound manager in a separate thread
  initTasks.push_back(Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
    std::cout << "Forge Game Engine - Creating Sound Manager\n";
    if(!SoundManager::Instance().init()) {
      std::cerr << "Forge Game Engine - Failed to initialize Sound Manager!" << std::endl;
      return false;
    }

    std::cout << "Forge Game Engine - Loading sounds and music\n";
    SoundManager::Instance().loadSFX("res/sfx", "sfx");
    SoundManager::Instance().loadMusic("res/music", "music");
    return true;
  }));

  // Initialize font manager in a separate thread
  initTasks.push_back(Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
    std::cout << "Forge Game Engine - Creating Font Manager\n";
    if(!FontManager::Instance().init()) {
      std::cerr << "Forge Game Engine - Failed to initialize Font Manager!" << std::endl;
      return false;
    }
    FontManager::Instance().loadFont("res/fonts", "fonts", 24);
    return true;
  }));

  // Initialize game state manager (on main thread because it directly calls rendering)
  std::cout << "Forge Game Engine - Creating Game State Manager and setting up initial Game States\n";
  mp_gameStateManager = new GameStateManager();
  if (!mp_gameStateManager) {
    std::cerr << "Forge Game Engine - Failed to create Game State Manager!" << std::endl;
    return false;
  }

  // Setting Up initial game states
  mp_gameStateManager->addState(std::make_unique<LogoState>());
  mp_gameStateManager->addState(std::make_unique<MainMenuState>());
  mp_gameStateManager->addState(std::make_unique<GamePlayState>());

  // Wait for all initialization tasks to complete
  bool allTasksSucceeded = true;
  for (auto& task : initTasks) {
    try {
      allTasksSucceeded &= task.get();
    } catch (const std::exception& e) {
      std::cerr << "Forge Game Engine - Initialization task failed: " << e.what() << std::endl;
      allTasksSucceeded = false;
    }
  }

  if (!allTasksSucceeded) {
    std::cerr << "Forge Game Engine - One or more initialization tasks failed" << std::endl;
    return false;
  }
  //_______________________________________________________________________________________________________________END

  std::cout << "Forge Game Engine - Game " << title <<  " initialized successfully!\n";
  std::cout << "Forge Game Engine - Running " << title << " <]==={}\n";

  //setting logo state for default state
  mp_gameStateManager->setState("LogoState");
  setRunning(true);  // Forge Game created successfully, start the main loop
  return true;
}

void GameEngine::handleEvents() {
  InputHandler::Instance().update();
}

void GameEngine::update() {
  // This method is now thread-safe and can be called from a worker thread
  std::lock_guard<std::mutex> lock(m_updateMutex);
  mp_gameStateManager->update();
  m_updateCompleted = true;
  m_updateCondition.notify_all();
}

void GameEngine::render() {
  // This should always run on the main thread due to OpenGL/SDL rendering context
  std::lock_guard<std::mutex> lock(m_renderMutex);
  SDL_RenderClear(mp_renderer);
  mp_gameStateManager->render();
  SDL_RenderPresent(mp_renderer);
}

void GameEngine::waitForUpdate() {
  std::unique_lock<std::mutex> lock(m_updateMutex);
  m_updateCondition.wait(lock, [this]{ return m_updateCompleted.load(); });
}

void GameEngine::signalUpdateComplete() {
  {
    std::lock_guard<std::mutex> lock(m_updateMutex);
    m_updateCompleted = false;  // Reset for next frame
  }
}

bool GameEngine::loadResourcesAsync(const std::string& path) {
  auto result = Forge::ThreadSystem::Instance().enqueueTaskWithResult(
    [this, path]() -> bool {
      // Example of async resource loading
      return TextureManager::Instance().load(path, "", mp_renderer);
    }
  );

  // You can either wait for the result or check it later
  try {
    return result.get(); // This blocks until the task completes
  } catch (const std::exception& e) {
    std::cerr << "Forge Game Engine - Resource loading failed: " << e.what() << std::endl;
    return false;
  }
}

void GameEngine::processBackgroundTasks() {
  // This method can be used to perform background processing
  // It should be safe to run on worker threads

  // Example: Process AI, physics, or other non-rendering tasks
  // These tasks can run while the main thread is handling rendering
}

void GameEngine::clean() {
  std::cout << "Forge Game Engine - Starting shutdown sequence...\n";

  // Wait for any pending background tasks to complete
  if(!Forge::ThreadSystem::Instance().isShutdown()) {
    std::cout << "Forge Game Engine - Waiting for background tasks to complete...\n";
    while(Forge::ThreadSystem::Instance().isBusy()) {
      SDL_Delay(1); // Short delay to avoid busy waiting
    }
  }

  // Clean up engine managers (non-singletons)
  std::cout << "Forge Game Engine - Cleaning up GameState manager...\n";
  if (mp_gameStateManager) {
    delete mp_gameStateManager;
    mp_gameStateManager = nullptr;
  }

  if (mp_textureManager) {
    delete mp_textureManager;
    mp_textureManager = nullptr;
  }

  // Save pointers to resources we'll clean up at the very end
  SDL_Window* window_to_destroy = mp_window;
  SDL_Renderer* renderer_to_destroy = mp_renderer;
  mp_window = nullptr;
  mp_renderer = nullptr;

  // Clean up singletons in reverse order of their initialization
  std::cout << "Forge Game Engine - Cleaning up Font Manager...\n";
  FontManager::Instance().clean();

  std::cout << "Forge Game Engine - Cleaning up Sound Manager...\n";
  SoundManager::Instance().clean();

  std::cout << "Forge Game Engine - Cleaning up Input Handler...\n";
  InputHandler::Instance().clean();

  std::cout << "Forge Game Engine - Cleaning up Texture Manager...\n";
  TextureManager::Instance().clean();

  // Clean up the thread system
  std::cout << "Forge Game Engine - Cleaning up Thread System...\n";
  if (!Forge::ThreadSystem::Instance().isShutdown()) {
    Forge::ThreadSystem::Instance().clean();
  }

  // Finally clean up SDL resources
  std::cout << "Forge Game Engine - Cleaning up SDL resources...\n";

  // Explicitly destroy renderer and window at the end, after all subsystems
  // are done using them
  if(renderer_to_destroy) {
    SDL_DestroyRenderer(renderer_to_destroy);
  }

  if(window_to_destroy) {
    SDL_DestroyWindow(window_to_destroy);
  }

#ifndef __APPLE__
  // Call SDL_Quit on non-Apple platforms -possibly a bug!!
  SDL_Quit();
#else
  // On macOS, skip SDL_Quit() to avoid crash
  std::cout << "Forge Game Engine - Skipping SDL_Quit() on macOS to prevent crash\n";
#endif

  std::cout << "Forge Game Engine - Shutdown complete!\n";
}
