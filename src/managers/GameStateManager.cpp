/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/GameStateManager.hpp"
#include "gameStates/GameState.hpp"
#include "core/Logger.hpp"
#include <algorithm>

// GameStateManager Implementation
GameStateManager::GameStateManager() : currentState() {
  // Reserve capacity for typical number of game states (performance optimization)
  states.reserve(8);
}

void GameStateManager::addState(std::unique_ptr<GameState> state) {
  // Check if a state with the same name already exists
  if (hasState(state->getName())) {
    GAMESTATE_ERROR("State with name " + state->getName() + " already exists");
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
    try {
      // Exit current state if exists
      if (auto current = currentState.lock()) {
        // Store the previous state name before changing (safe access via weak_ptr)
        std::string prevStateName = current->getName();
        GAMESTATE_INFO("Exiting state: " + prevStateName);
        bool exitSuccess = current->exit();
        if (!exitSuccess) {
          GAMESTATE_WARN("Exit for state " + prevStateName + " returned false");
        }
        currentState.reset(); // Clear weak_ptr before entering new state
      }

      // Set new current state (non-owning observer via weak_ptr)
      currentState = *it;

      // Trigger enter of new state
      if (auto current = currentState.lock()) {
        GAMESTATE_INFO("Entering state: " + stateName);
        bool enterSuccess = current->enter();
        if (!enterSuccess) {
          GAMESTATE_ERROR("Enter for state " + stateName + " failed");
          currentState.reset(); // Clear invalid state
        }
      }
    } catch (const std::exception& e) {
      GAMESTATE_ERROR("Exception during state transition: " + std::string(e.what()));
      currentState.reset(); // Safety measure
    } catch (...) {
      GAMESTATE_ERROR("Unknown exception during state transition");
      currentState.reset(); // Safety measure
    }
  } else {
    GAMESTATE_ERROR("State not found: " + stateName);

    // Exit current state if it exists
    if (auto current = currentState.lock()) {
      try {
        current->exit();
      } catch (...) {
        GAMESTATE_ERROR("Exception while exiting state");
      }
      currentState.reset();
    }
  }
}

void GameStateManager::update(float deltaTime) {
  m_lastDeltaTime = deltaTime; // Store deltaTime for render
  if (auto current = currentState.lock()) {
    current->update(deltaTime);
  }
}

void GameStateManager::render() {
  if (auto current = currentState.lock()) {
    current->render(m_lastDeltaTime);
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
