#include "GameEngine.hpp"
#include <SDL3/SDL.h>
#include <iostream>
#include <string>

const float FPS {60.0f};
const float DELAY_TIME{ 1000.0f / FPS };
const int WINDOW_WIDTH{1920};
const int WINDOW_HEIGHT{1080};
const std::string GAME_NAME{"Game Template"};

int main(int argc, char* argv[]) {

    Uint64 frameStart, frameTime;

    std::cout << "Forge Game Engine - Initializing " << GAME_NAME <<"...\n";

    if(GameEngine::Instance()->init(GAME_NAME.c_str() ,WINDOW_WIDTH, WINDOW_HEIGHT, false)){
        while (GameEngine::Instance()->getRunning()) {

            frameStart = SDL_GetTicks();

            GameEngine::Instance()->handleEvents();
            GameEngine::Instance()->update();
            GameEngine::Instance()->render();

            frameTime = SDL_GetTicks() - frameStart;

            if (frameTime < DELAY_TIME) {
                SDL_Delay((int)(DELAY_TIME - frameTime));
            }
        }
    }else{
        std::cout << "Forge Game - Init " << GAME_NAME << " Failed!:" << SDL_GetError();

        return -1;
    }

    std::cout << "Forge Game " << GAME_NAME << " Shutting down...\n";

        GameEngine::Instance()->clean();

        return 0;
}
