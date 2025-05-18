/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/GameStateManager.hpp"
#include "states/GameState.hpp"
#include <algorithm>
#include <iostream>

// GameStateManager Implementation
GameStateManager::GameStateManager() : currentState(nullptr) {}

void GameStateManager::addState(std::unique_ptr<GameState> state) {
  // Check if a state with the same name already exists
  if (hasState(state->getName())) {
    throw std::runtime_error("Forge Game Engine - State with name " + state->getName() + " already exists");
  }
  states.push_back(std::move(state));
}

void GameStateManager::setState(const std::string& stateName) {
  // Find the new state
  auto it = std::find_if(states.begin(), states.end(),
                         [&stateName](const std::unique_ptr<GameState>& state) {
                           return state->getName() == stateName;
                         });

  if (it != states.end()) {
    // Exit current state if exists
    if (currentState) {
      currentState->exit();
    }

    // Transfer ownership correctly
    currentState = it->get();

    // Trigger enter of new state
    if (currentState) {
      currentState->enter();
    }
  } else {
    std::cerr << "Forge Game Engine - State not found: " << stateName << std::endl;

    // Exit current state if it exists
    if (currentState) {
      currentState->exit();
      currentState = nullptr;
    }
  }
}

void GameStateManager::update() {
  if (currentState) {
    currentState->update();
  }
}

void GameStateManager::render() {
  if (currentState) {
    currentState->render();
  }
}

bool GameStateManager::hasState(const std::string& stateName) const {
  return std::any_of(states.begin(), states.end(),
                     [&stateName](const std::unique_ptr<GameState>& state) {
                       return state->getName() == stateName;
                     });
}

GameState* GameStateManager::getState(const std::string& stateName) const {
  auto it = std::find_if(states.begin(), states.end(),
                         [&stateName](const std::unique_ptr<GameState>& state) {
                           return state->getName() == stateName;
                         });

  return it != states.end() ? it->get() : nullptr;
}

void GameStateManager::removeState(const std::string& stateName) {
  // If trying to remove the current state, first clear the current state
  if (currentState && currentState->getName() == stateName) {
    currentState->exit();
    currentState = nullptr;
  }

  // Remove the state from the vector
  auto it =
      std::remove_if(states.begin(), states.end(),
                     [&stateName](const std::unique_ptr<GameState>& state) {
                       return state->getName() == stateName;
                     });

  states.erase(it, states.end());
}

void GameStateManager::clearAllStates() {
  // Exit current state if exists
  if (currentState) {
    currentState->exit();
    currentState = nullptr;
  }

  // Clear all states
  states.clear();
}
