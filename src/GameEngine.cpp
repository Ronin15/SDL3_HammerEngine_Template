#include "GameEngine.hpp"
#include "InputHandler.hpp"
#include "SDL3/SDL_init.h"
#include <iostream>

GameEngine* GameEngine::sp_Instance{nullptr};

bool GameEngine::init(const char* title, int width, int height, bool fullscreen) {

    // attempt to initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO)) {

        std::cout << "Forge Game Engine framework online!\n";
        fullscreen = false;
        SDL_Rect display;
        SDL_GetDisplayBounds(1, &display);

            if (width >= display.w && height >= display.h) {

                fullscreen = false;//false for Troubleshooting. True for actual full screen.

                std::cout << "Forge Game Engine window size set to full screen!\n";
        }
                std::cout << "Forge Game Engine detected resolution on monitor 1 : " << display.w << "x" << display.h << "\n";

        int flags{0};

        if (fullscreen) {
            flags = SDL_WINDOW_FULLSCREEN;
        }

        p_Window = SDL_CreateWindow(title, width, height, flags);

        if (p_Window){

            std::cout << "Forge Game Engine window creation system online!\n";
            p_Renderer = SDL_CreateRenderer(p_Window,NULL);

            if (p_Renderer) {

                std::cout << "Forge Game Engine rendering system online!\n";
				SDL_SetRenderDrawColor(p_Renderer, 31, 32, 34, 255); // Forge Game Engine gunmetal dark grey

            }
            else {

                std::cout << "Forge Game Engine rendering system creation failed! " << SDL_GetError();
                std::cout << "Press Enter to Continue";

                return false; // Forge renderer fail
            }

        }
        else {

            std::cout << "Forge Game Engine window system creation failed! Maybe need a window Manager? " << SDL_GetError();
            std::cout << "Press Enter to Continue\n";

            return false; // Forge window fail
        }
    }
    else {
        std::cerr << "Forge Game Engine framework creation failed! Make sure you have the SDL3 runtime installed? SDL error: " << SDL_GetError() << std::endl;
        std::cout << "Press Enter to Continue\n";
        return false; // Forge SDL init fail. Make sure you have the SDL2 runtime
        // installed.
    }

    InputHandler::Instance()->initializeGamePad(); // aligned here for organization sake.
    std::cout << "Forge Game Engine creating game constructs.... \n";
    //_______________________________________________________________________________________________________________BEGIN
    // Loading intiial game states and constructs








    //_______________________________________________________________________________________________________________END

    setRunning(true); // Forge Game created successfully, start the main loop
    std::cout << "Forge Game Engine game contructs created successfully!\n";
    std::cout << "Forge Game Engine game intialized successfully!\n";
    std::cout << "Forge Game Engine running " << title << " <]==={}\n";

    return true;
}

void GameEngine::handleEvents() {

    InputHandler::Instance()->update();
}

void GameEngine::update() {

}

void GameEngine::render() {

    SDL_RenderClear(p_Renderer);

    //draw hear

    SDL_RenderPresent(p_Renderer);
}

void GameEngine::clean() {
    InputHandler::Instance()->clean();
    SDL_DestroyWindow(p_Window);
    SDL_DestroyRenderer(p_Renderer);
    SDL_Quit();

    std::cout << "Forge Game Engine shutdown!\n";
}
