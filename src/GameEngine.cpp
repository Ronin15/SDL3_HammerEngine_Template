#include "GameEngine.hpp"
#include "GamePlayState.hpp"
#include "GameStateManager.hpp"
#include "InputHandler.hpp"
#include "LogoState.hpp"
#include "MainMenuState.hpp"
#include "SoundManager.hpp"

#include <iostream>

GameEngine* GameEngine::sp_Instance{nullptr};

bool GameEngine::init(const char* title, int width, int height, bool fullscreen) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    std::cout << "Forge Game Engine - Framework SDL3 online!\n";
    fullscreen = false;
    SDL_Rect display;
    SDL_GetDisplayBounds(1, &display);

    std::cout << "Forge Game Engine - Detected resolution on monitor 1 : "
              << display.w << "x" << display.h << "\n";

    if (width >= display.w || height >= display.h) {
      fullscreen =
          false;  // false for Troubleshooting. True for actual full screen.

      std::cout << "Forge Game Engine - Window size set to Full Screen!\n";
    }
    int flags{0};

    if (fullscreen) {
      flags = SDL_WINDOW_FULLSCREEN;
    }

    p_window = SDL_CreateWindow(title, width, height, flags);

    if (p_window) {
      std::cout << "Forge Game Engine - Window creation system online!\n";
      p_renderer = SDL_CreateRenderer(p_window, NULL);

      if (p_renderer) {
        std::cout << "Forge Game Engine - Rendering system online!\n";
        SDL_SetRenderDrawColor(p_renderer, 31, 32, 34,
                               255);  // Forge Game Engine gunmetal dark grey

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
  //_______________________________________________________________________________________________________________BEGIN
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
  std::cout << "Forge Game Engine - Initializing Sound Manager.... \n";
  // Initialize the sound manager
  SoundManager::Instance()->init();

  std::cout << "Forge Game Engine - Loading sounds and music.... \n";
  SoundManager::Instance()->loadSFX("res/sfx", "sfx");

  // Initialize game state manager
  std::cout << "Forge Game Engine - Creating Game State Manager and setting up game states.... \n";
  mp_gameStateManager = new GameStateManager();
  if (!mp_gameStateManager) {
    std::cerr << "Forge Game Engine - Failed to create Game State Manager!\n";
    return false;
  }
  // Setting Up game states
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

  InputHandler::Instance()->clean();
  SoundManager::Instance()->clean();
  TextureManager::Instance()->clean();

  SDL_DestroyWindow(p_window);
  SDL_DestroyRenderer(p_renderer);
  SDL_Quit();
  std::cout << "Forge Game Engine - Shutdown!\n";
}
