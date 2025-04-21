#include "GameEngine.hpp"
#include "GamePlayState.hpp"
#include "GameStateManager.hpp"
#include "FontManager.hpp"
#include "InputHandler.hpp"
#include "LogoState.hpp"
#include "MainMenuState.hpp"
#include "SoundManager.hpp"
#include <SDL3_image/SDL_image.h>
#include <iostream>
#include <unistd.h>

GameEngine* GameEngine::sp_Instance{nullptr};
#define FORGE_GRAY 31, 32, 34, 255

bool GameEngine::init(const char* title, int width, int height, bool fullscreen) {
  if (SDL_Init(SDL_INIT_VIDEO)) {
    std::cout << "Forge Game Engine - Framework SDL3 online!\n";

    // Get display bounds to determine optimal window size
    SDL_Rect display;
    if (SDL_GetDisplayBounds(1, &display) != 0) { // Try display 1 first
      // Try display 0 as fallback
      if (SDL_GetDisplayBounds(0, &display) != 0) {
        std::cout << "Forge Game Engine - Warning: Could not get display bounds: " << SDL_GetError() << "\n";
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
          fullscreen = true;
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
        fullscreen = true;
        std::cout << "Forge Game Engine - Window size larger than screen, enabling fullscreen\n";
      }
    }
    // Fullscreen handling
    int flags{0};

    if (fullscreen) {
      flags = SDL_WINDOW_FULLSCREEN;
      std::cout << "Forge Game Engine - Window size set to Full Screen!\n";
    }

    p_window = SDL_CreateWindow(title, m_windowWidth, m_windowHeight, flags);

    if (p_window) {
      std::cout << "Forge Game Engine - Window creation system online!\n";

      // Set window icon
      std::cout << "Forge Game Engine - Setting window icon...\n";

      // Use SDL_image to directly load the icon
      // Multiple paths are tried to ensure the icon can be found regardless of current directory
      SDL_Surface* iconSurface = nullptr;
      const char* iconPath = "res/img/icon.ico";

        iconSurface = IMG_Load(iconPath);
        if (iconSurface) {
          std::cout << "Forge Game Engine - Loaded icon from: " << iconPath << "\n";
        }

      if (iconSurface) {
        SDL_SetWindowIcon(p_window, iconSurface);
        SDL_DestroySurface(iconSurface);
        std::cout << "Forge Game Engine - Window icon set successfully!\n";
      } else {
        std::cout << "Forge Game Engine - Failed to load window icon: " << SDL_GetError() << "\n";
      }

      p_renderer = SDL_CreateRenderer(p_window, NULL);

      if (p_renderer) {
        std::cout << "Forge Game Engine - Rendering system online!\n";
        SDL_SetRenderDrawColor(p_renderer, FORGE_GRAY);  // Forge Game Engine gunmetal dark grey

      } else {
        std::cout << "Forge Game Engine - Rendering system creation failed! "
                  << SDL_GetError();
        return false;  // Forge renderer fail
      }

    } else {
      std::cout << "Forge Game Engine- Window system creation failed! Maybe "
                   "need a window Manager?"
                << SDL_GetError();
      return false;  // Forge window fail
    }
  } else {
    std::cerr << "Forge Game Engine - Framework creation failed! Make sure you "
                 "have the SDL3 runtime installed? SDL error: "
              << SDL_GetError() << std::endl;
    return false;  // Forge SDL init fail. Make sure you have the SDL3 runtime
                   // installed.
  }
  //INITIALIZING GAME RESOURCE MANAGEMENT_________________________________________________________________________________BEGIN
  std::cout << "Forge Game Engine - Detecting and initializing "
               "gamepad/controller.... \n";
  InputHandler::Instance()->initializeGamePad();  // aligned here for organization sake.
  std::cout << "Forge Game Engine - Creating Texture Manager.... \n";
  // load textures
  mp_textureManager = new TextureManager();
  if (!mp_textureManager) {
    std::cerr << "Forge Game Engine - Failed to create Texture Manager!\n";
    return false;
  }
  std::cout << "Forge Game Engine - Creating and loading textures.... \n";
  TextureManager::Instance()->load("res/img", "", p_renderer);
  std::cout << "Forge Game Engine - Creating Sound Manager.... \n";
  // Initialize the sound manager
  SoundManager::Instance()->init();
  std::cout << "Forge Game Engine - Loading sounds and music.... \n";
  SoundManager::Instance()->loadSFX("res/sfx", "sfx");
  SoundManager::Instance()->loadMusic("res/music", "music");

  std::cout << "Forge Game Engine - Creating Font Manager.... \n";
  // Initialize the font manager
  FontManager::Instance()->init();
  FontManager::Instance()->loadFont("res/fonts", "fonts", 24);

  // Initialize game state manager
  std::cout << "Forge Game Engine - Creating Game State Manager and setting up initial game states.... \n";
  mp_gameStateManager = new GameStateManager();
  if (!mp_gameStateManager) {
    std::cerr << "Forge Game Engine - Failed to create Game State Manager!\n";
    return false;
  }
  // Setting Up initial game states
  mp_gameStateManager->addState(std::make_unique<LogoState>());
  mp_gameStateManager->addState(std::make_unique<MainMenuState>());
  mp_gameStateManager->addState(std::make_unique<GamePlayState>());

  //_______________________________________________________________________________________________________________END

  setRunning(true);  // Forge Game created successfully, start the main loop
  std::cout << "Forge Game Engine - Game constructs created successfully!\n";
  std::cout << "Forge Game Engine - Game initialized successfully!\n";
  std::cout << "Forge Game Engine - Running " << title << " <]==={}\n";
  //setting logo state for default state
  mp_gameStateManager->setState("LogoState");
  return true;
}

void GameEngine::handleEvents() {
  InputHandler::Instance()->update();
}

void GameEngine::update() {
  mp_gameStateManager->update();
}

void GameEngine::render() {
  SDL_RenderClear(p_renderer);

  mp_gameStateManager->render();

  SDL_RenderPresent(p_renderer);
}

void GameEngine::clean() {
  if (mp_gameStateManager) {
    delete mp_gameStateManager;
    mp_gameStateManager = nullptr;
  }

  FontManager::Instance()->clean();
  InputHandler::Instance()->clean();
  SoundManager::Instance()->clean();
  TextureManager::Instance()->clean();

  SDL_DestroyWindow(p_window);
  SDL_DestroyRenderer(p_renderer);
  SDL_Quit();
  std::cout << "Forge Game Engine - Shutdown!\n";
}
