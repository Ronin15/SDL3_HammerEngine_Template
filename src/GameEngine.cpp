#include "GameEngine.hpp"
#include <cstddef>
#include <iostream>
#include "InputHandler.hpp"
#include "SDL3/SDL_init.h"

GameEngine* GameEngine::sp_Instance{nullptr};

bool GameEngine::init(const char* title, int width, int height, bool fullscreen) {

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    std::cout << "Forge Game Engine - Framework SDL3 online!\n";
    fullscreen = false;
    SDL_Rect display;
    SDL_GetDisplayBounds(1, &display);

    std::cout << "Forge Game Engine - Detected resolution on monitor 1 : " << display.w << "x" << display.h << "\n";

    if (width >= display.w || height >= display.h) {
      fullscreen =
          true;  // false for Troubleshooting. True for actual full screen.

      std::cout << "Forge Game Engine - Window size set to Full Screen!\n";
    }
    int flags{0};

    if (fullscreen) {
      flags = SDL_WINDOW_FULLSCREEN;
    }

    p_Window = SDL_CreateWindow(title, width, height, flags);

    if (p_Window) {
      std::cout << "Forge Game Engine - Window creation system online!\n";
      p_Renderer = SDL_CreateRenderer(p_Window, NULL);

      if (p_Renderer) {
        std::cout << "Forge Game Engine - Rendering system online!\n";
        SDL_SetRenderDrawColor(p_Renderer, 31, 32, 34,
                               255);  // Forge Game Engine gunmetal dark grey

      } else {
        std::cout << "Forge Game Engine - Rendering system creation failed! " << SDL_GetError();
        return false;  // Forge renderer fail
      }

    } else {
      std::cout << "Forge Game Engine- Window system creation failed! Maybe need a window Manager?" << SDL_GetError();
      return false;  // Forge window fail
    }
  } else {
    std::cerr << "Forge Game Engine - Framework creation failed! Make sure you have the SDL3 runtime installed? SDL error: " << SDL_GetError() << std::endl;
    return false;  // Forge SDL init fail. Make sure you have the SDL3 runtime installed.
  }

  InputHandler::Instance()->initializeGamePad();  // aligned here for organization sake.
  std::cout << "Forge Game Engine - Creating game constructs.... \n";
  //_______________________________________________________________________________________________________________BEGIN
  // Loading intiial game states and constructs

  //_______________________________________________________________________________________________________________END

  setRunning(true);  // Forge Game created successfully, start the main loop
  std::cout << "Forge Game Engine - Game constructs created successfully!\n";
  std::cout << "Forge Game Engine - Game initialized successfully!\n";
  std::cout << "Forge Game Engine - Running " << title << " <]==={}\n";

  return true;
}

void GameEngine::handleEvents() {
  InputHandler::Instance()->update();
}

void GameEngine::update() {}

void GameEngine::render() {
  SDL_RenderClear(p_Renderer);

  // draw hear

  SDL_RenderPresent(p_Renderer);
}

void GameEngine::clean() {
  InputHandler::Instance()->clean();
  SDL_DestroyWindow(p_Window);
  SDL_DestroyRenderer(p_Renderer);
  SDL_Quit();
  std::cout << "Forge Game Engine - Shutdown!\n";
}
