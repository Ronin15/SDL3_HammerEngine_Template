/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/GameStateManager.hpp"
#include "core/Logger.hpp"
#include "gameStates/GameState.hpp"
#include <algorithm>
#include <format>
#include <stdexcept>

// Forward declaration for SDL renderer access
extern "C" {
#include <SDL3/SDL.h>
}

// GameStateManager Implementation
GameStateManager::GameStateManager() {
  // Reserve capacity for typical number of game states (performance
  // optimization)
  m_registeredStates.reserve(8);
  m_activeStates.reserve(3); // For the active stack
}

void GameStateManager::addState(std::unique_ptr<GameState> state) {
  const std::string name = state->getName();
  if (hasState(name)) {
    GAMESTATE_ERROR(std::format("State with name {} already exists", name));
    throw std::runtime_error(std::format("Hammer Game Engine - State with name {} already exists", name));
  }
  // Set state manager reference so state can access transitions and frame data
  state->setStateManager(this);
  // Move the state into the map as shared_ptr
  m_registeredStates[name] = std::shared_ptr<GameState>(state.release());
}

void GameStateManager::pushState(const std::string &stateName) {
  auto it = m_registeredStates.find(stateName);
  if (it != m_registeredStates.end()) {
    // Pause the current top state if it exists
    if (!m_activeStates.empty()) {
      m_activeStates.back()->pause();
    }

    // CRITICAL: Enter the state BEFORE adding to active stack to prevent rendering before initialization
    auto newState = it->second;
    if (!newState->enter()) {
      GAMESTATE_ERROR(std::format("Failed to enter state: {}", stateName));
      return;
    }

    // Now push the fully initialized state onto the stack
    m_activeStates.push_back(newState);
    GAMESTATE_INFO(std::format("Pushed state: {}", stateName));
  } else {
    GAMESTATE_ERROR(std::format("State not found: {}", stateName));
  }
}

void GameStateManager::popState() {
  if (!m_activeStates.empty()) {
    // CRITICAL: Wait for exit to complete BEFORE removing from stack
    auto currentState = m_activeStates.back();
    currentState->exit(); // Wait for exit to complete fully
    
    // Only remove after exit is complete
    m_activeStates.pop_back();
    GAMESTATE_INFO("Popped state");

    // Resume the new top state if it exists
    if (!m_activeStates.empty()) {
      m_activeStates.back()->resume();
    }
  }
}

void GameStateManager::changeState(const std::string &stateName) {
  // Pop the current state if one exists (waits for exit to complete)
  if (!m_activeStates.empty()) {
    popState();
  }
  // Push the new state (waits for enter to complete)
  pushState(stateName);
}

void GameStateManager::update(float deltaTime) {
  m_lastDeltaTime = deltaTime; // Store deltaTime for render
  
  // Only update the top state when multiple states are active (e.g., PauseState over GamePlayState)
  // This prevents underlying states from processing game logic when paused
  if (!m_activeStates.empty()) {
    m_activeStates.back()->update(deltaTime);
  }
}

void GameStateManager::render(SDL_Renderer* renderer, float interpolationAlpha) {
  // Only render the current active state (top of stack)
  // Pause functionality preserves the previous state but doesn't render it
  if (!m_activeStates.empty()) {
    m_activeStates.back()->render(renderer, interpolationAlpha);
  }
}

void GameStateManager::handleInput() {
  // Only the top state handles input
  if (!m_activeStates.empty()) {
    m_activeStates.back()->handleInput();
  }
}

bool GameStateManager::hasState(const std::string &stateName) const {
  return m_registeredStates.find(stateName) != m_registeredStates.end();
}

std::shared_ptr<GameState>
GameStateManager::getState(const std::string &stateName) const {
  auto it = m_registeredStates.find(stateName);
  return it != m_registeredStates.end() ? it->second : nullptr;
}

void GameStateManager::removeState(const std::string &stateName) {
  // First, remove the state from the active stack if it's there
  m_activeStates.erase(
      std::remove_if(m_activeStates.begin(), m_activeStates.end(),
                     [&](const std::shared_ptr<GameState> &state) {
                       if (state->getName() == stateName) {
                         state->exit();
                         return true;
                       }
                       return false;
                     }),
      m_activeStates.end());

  // Resume the new top state if it exists
  if (!m_activeStates.empty()) {
    m_activeStates.back()->resume();
  }

  // Remove from the registered states map
  m_registeredStates.erase(stateName);
}

void GameStateManager::clearAllStates() {
  // Exit all active states
  for (const auto &state : m_activeStates) {
    state->exit();
  }
  m_activeStates.clear();
  m_registeredStates.clear();
}
