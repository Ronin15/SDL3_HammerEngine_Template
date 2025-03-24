#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <iostream>

const int WINDOW_WIDTH {1920};
const int WINDOW_HEIGHT {1080};

int main(int argc, char* argv[]) {
    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Create window
    SDL_Window* window = SDL_CreateWindow("SDL3 Hello World", WINDOW_WIDTH, WINDOW_HEIGHT,0);
    if (!window) {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    // Create renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Main loop flag
    bool quit = false;

    // Event handler
    SDL_Event event;

    // Main loop
    while (!quit) {
        // Handle events on queue
        while (SDL_PollEvent(&event)) {
            // User requests quit
            if (event.type == SDL_EVENT_QUIT) {
                quit = true;

                std::cout << "Quitting" << std::endl;
            }
        }

        // Clear screen
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        // Draw a white rectangle in the middle of the screen
        SDL_FRect rect;
        rect.x = 300;
        rect.y = 300;
        rect.w = 100;
        rect.h = 100;
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(renderer, &rect);

        // Update screen
        SDL_RenderPresent(renderer);

        // Add a small delay to prevent CPU overuse
        SDL_Delay(16);
    }

    // Clean up
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
