#include "GamePlayState.hpp"
#include "GameStateManager.hpp"
#include "InputHandler.hpp"
#include "GameEngine.hpp"
#include "PauseState.hpp"
#include "TextureManager.hpp"
#include <iostream>

bool GamePlayState::enter() {
  std::cout << "Forge Game Engine - Entering GAME State" << std::endl;

  // Reset transition flag when entering
  m_transitioningToPause = false;

  // Remove PauseState if we're coming from it
  if (GameEngine::Instance()->getGameStateManager()->hasState("PauseState")) {
    GameEngine::Instance()->getGameStateManager()->removeState("PauseState");
    std::cout << "Forge Game Engine - Removing PAUSE State" << std::endl;
  }

  // Check if ForgeEngine texture is already loaded
  // If we're coming from pause, it should still be in the map
  // Try to load the texture only if it's not already loaded
  if (!TextureManager::Instance()->isTextureInMap("ForgeEngine")) {
    std::cout << "Forge Game Engine - Texture not in map, loading it" << std::endl;

    // Load the game play state asset
    if (!TextureManager::Instance()->load("res/img/ForgeEngine.png", "ForgeEngine", GameEngine::Instance()->getRenderer())) {
      // Failed to load texture
      std::cout << "Forge Game Engine - Failed to load ForgeEngine texture" << std::endl;
      return false; // Return false to indicate enter() failed
    }
    std::cout << "Forge Game Engine - Successfully loaded ForgeEngine texture" << std::endl;
  } else {
    std::cout << "Forge Game Engine - Texture already loaded, skipping load" << std::endl;
  }

  return true;
}

void GamePlayState::update() {
    //std::cout << "Updating GAME State" << std::endl;
  // Handle pause and ESC key.
  if (InputHandler::Instance()->isKeyDown(SDL_SCANCODE_P)) {
      // Create PauseState if it doesn't exist
      if (!GameEngine::Instance()->getGameStateManager()->hasState("PauseState")) {
          GameEngine::Instance()->getGameStateManager()->addState(std::make_unique<PauseState>());
          std::cout << "Forge Game Engine - Created PAUSE State" << std::endl;
      }
      m_transitioningToPause = true; // Set flag before transitioning
      GameEngine::Instance()->getGameStateManager()->setState("PauseState");
  }
  if (InputHandler::Instance()->isKeyDown(SDL_SCANCODE_ESCAPE)) {
      GameEngine::Instance()->setRunning(false);
  }
}

void GamePlayState::render() {
  //std::cout << "Rendering GAME State" << std::endl;
}
bool GamePlayState::exit() {
  std::cout << "Forge Game Engine - Exiting GAME State" << std::endl;

  // Only clear texture if we're not transitioning to pause state
  if (!m_transitioningToPause) {
    TextureManager::Instance()->clearFromTexMap("ForgeEngine");
    std::cout << "Forge Game Engine - Cleared texture, not going to pause" << std::endl;
  } else {
    std::cout << "Forge Game Engine - Keeping texture, going to pause" << std::endl;
    // Reset flag for next time
    m_transitioningToPause = false;
  }

  return true;
}

std::string GamePlayState::getName() const {
  return "GamePlayState";
}
