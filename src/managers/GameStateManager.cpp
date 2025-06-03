/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/GameStateManager.hpp"
#include "gameStates/GameState.hpp"
#include <algorithm>
#include <iostream>

// GameStateManager Implementation
GameStateManager::GameStateManager() : currentState() {}

void GameStateManager::addState(std::unique_ptr<GameState> state) {
  // Check if a state with the same name already exists
  if (hasState(state->getName())) {
    throw std::runtime_error("Forge Game Engine - State with name " + state->getName() + " already exists");
  }
  // Convert unique_ptr to shared_ptr and add to container
  states.push_back(std::shared_ptr<GameState>(state.release()));
}

void GameStateManager::setState(const std::string& stateName) {
  // Find the new state
  auto it = std::find_if(states.begin(), states.end(),
                         [&stateName](const std::shared_ptr<GameState>& state) {
                           return state->getName() == stateName;
                         });

  if (it != states.end()) {
    // Store the previous state name before changing (safe access via weak_ptr)
    std::string prevStateName = "None";
    if (auto current = currentState.lock()) {
      prevStateName = current->getName();
    }
    
    try {
      // Exit current state if exists
      if (auto current = currentState.lock()) {
        std::cout << "Forge Game Engine - Exiting state: " << prevStateName << std::endl;
        bool exitSuccess = current->exit();
        if (!exitSuccess) {
          std::cerr << "Forge Game Engine - Warning: Exit for state " << prevStateName << " returned false" << std::endl;
        }
        currentState.reset(); // Clear weak_ptr before entering new state
      }

      // Set new current state (non-owning observer via weak_ptr)
      currentState = *it;

      // Trigger enter of new state
      if (auto current = currentState.lock()) {
        std::cout << "Forge Game Engine - Entering state: " << stateName << std::endl;
        bool enterSuccess = current->enter();
        if (!enterSuccess) {
          std::cerr << "Forge Game Engine - Error: Enter for state " << stateName << " failed" << std::endl;
          currentState.reset(); // Clear invalid state
        }
      }
    } catch (const std::exception& e) {
      std::cerr << "Forge Game Engine - Exception during state transition: " << e.what() << std::endl;
      currentState.reset(); // Safety measure
    } catch (...) {
      std::cerr << "Forge Game Engine - Unknown exception during state transition" << std::endl;
      currentState.reset(); // Safety measure
    }
  } else {
    std::cerr << "Forge Game Engine - State not found: " << stateName << std::endl;

    // Exit current state if it exists
    if (auto current = currentState.lock()) {
      try {
        current->exit();
      } catch (...) {
        std::cerr << "Forge Game Engine - Exception while exiting state" << std::endl;
      }
      currentState.reset();
    }
  }
}

void GameStateManager::update(float deltaTime) {
  if (auto current = currentState.lock()) {
    current->update(deltaTime);
  }
}

void GameStateManager::render() {
  if (auto current = currentState.lock()) {
    current->render();
  }
}

bool GameStateManager::hasState(const std::string& stateName) const {
  return std::any_of(states.begin(), states.end(),
                     [&stateName](const std::shared_ptr<GameState>& state) {
                       return state->getName() == stateName;
                     });
}

std::shared_ptr<GameState> GameStateManager::getState(const std::string& stateName) const {
  auto it = std::find_if(states.begin(), states.end(),
                         [&stateName](const std::shared_ptr<GameState>& state) {
                           return state->getName() == stateName;
                         });

  return it != states.end() ? *it : nullptr;
}

void GameStateManager::removeState(const std::string& stateName) {
  // If trying to remove the current state, first clear the current state
  if (auto current = currentState.lock()) {
    if (current->getName() == stateName) {
      current->exit();
      currentState.reset();
    }
  }

  // Remove the state from the vector
  auto it =
      std::remove_if(states.begin(), states.end(),
                     [&stateName](const std::shared_ptr<GameState>& state) {
                       return state->getName() == stateName;
                     });

  states.erase(it, states.end());
}

void GameStateManager::clearAllStates() {
  // Exit current state if exists
  if (auto current = currentState.lock()) {
    current->exit();
    currentState.reset();
  }

  // Clear all states
  states.clear();
}
