#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <chrono>
#include <iostream>
#include <thread>
#include "EntityIdleState.hpp"
#include "EntityJumpingState.hpp"
#include "EntityRunningState.hpp"
#include "EntityStateManager.hpp"
#include "EntityWalkingState.hpp"
#include "GamePlayState.hpp"
#include "GameStateManager.hpp"
#include "MainMenuState.hpp"
#include "PauseState.hpp"

const int WINDOW_WIDTH{1920};
const int WINDOW_HEIGHT{1080};
const char* GAME_NAME = "Galaxy Forge";

// Simulated game loop
void simulateGameLoop() {
  // Create state manager
  GameStateManager gameStateManager;
  EntityStateManager entityStateManager;

  // Add all possible states
  gameStateManager.addState(std::make_unique<MainMenuState>());
  gameStateManager.addState(std::make_unique<GamePlayState>());
  gameStateManager.addState(std::make_unique<PauseState>());

  // add all possible entitiy states
  entityStateManager.addState("Idle", std::make_unique<EntityIdleState>());
  entityStateManager.addState("Walking",std::make_unique<EntityWalkingState>());
  entityStateManager.addState("Running",std::make_unique<EntityRunningState>());
  entityStateManager.addState("Jumping",std::make_unique<EntityJumpingState>());

  // Simulate game flow
  std::cout << "Starting game simulation..." << std::endl;

  // Start in Main Menu
  gameStateManager.setState("MainMenuState");
  gameStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Transition to Gameplay
  gameStateManager.setState("GamePlayState");
  gameStateManager.update();
  entityStateManager.setState("Idle");
  entityStateManager.update();
  entityStateManager.setState("Walking");
  entityStateManager.update();
  entityStateManager.setState("Running");
  entityStateManager.update();
  entityStateManager.setState("Jumping");
  entityStateManager.update();

  std::this_thread::sleep_for(std::chrono::seconds(3));

  // Pause the game
  gameStateManager.setState("PauseState");
  gameStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Resume gameplay
  gameStateManager.setState("GamePlayState");
  gameStateManager.update();
  entityStateManager.setState("Idle");
  entityStateManager.update();
  entityStateManager.setState("Walking");
  entityStateManager.update();
  entityStateManager.setState("Running");
  entityStateManager.update();
  entityStateManager.setState("Jumping");
  entityStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(3));

  // Return to main menu
  gameStateManager.setState("MainMenuState");
  gameStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Try changing to a non-existent state to test error handling
  try {
    gameStateManager.setState("NonExistentState");
  } catch (const std::exception& e) {
    std::cerr << "Error changing state: " << e.what() << std::endl;
  }

  // Remove a state and verify
  std::cout << "Removing PauseState..." << std::endl;
  gameStateManager.removeState("PauseState");

  // Verify state removal
  if (!gameStateManager.hasState("PauseState")) {
    std::cout << "PauseState successfully removed." << std::endl;
  }

  // Final state cleanup
  gameStateManager.clearAllStates();
  std::cout << "Game simulation complete." << std::endl;
}

int main(int argc, char* argv[]) {
  /*   // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError()
    << std::endl; return 1;
    }

    // Create window
    SDL_Window* window = SDL_CreateWindow("SDL3 Hello World", WINDOW_WIDTH,
    WINDOW_HEIGHT,0); if (!window) { std::cerr << "Window could not be created!
    SDL_Error: " << SDL_GetError() << std::endl; SDL_Quit(); return 1;
    }

    // Create renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        std::cerr << "Renderer could not be created! SDL_Error: " <<
    SDL_GetError() << std::endl; SDL_DestroyWindow(window); SDL_Quit(); return
    1;
    }

    // Main loop flag
    bool quit = false;

    // Event handler
    SDL_Event event;

    // Main loop
    while (!quit) {

        //simulateGameLoop();
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
*/
  for (int i = 1; i < 15; i++) {
    simulateGameLoop();
    std::cout << i << " Simulated loop(s) complete..\n" << std::endl;
  }
  return 0;
}
