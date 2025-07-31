#include "managers/GameStateManager.hpp"
#include "core/Logger.hpp"
#include "gameStates/GameState.hpp"
#include <algorithm>
#include <stdexcept>

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
    GAMESTATE_ERROR("State with name " + name + " already exists");
    throw std::runtime_error("Hammer Game Engine - State with name " + name +
                             " already exists");
  }
  // Move the state into the map
  m_registeredStates[name] = std::move(state);
}

void GameStateManager::pushState(const std::string &stateName) {
  auto it = m_registeredStates.find(stateName);
  if (it != m_registeredStates.end()) {
    // Pause the current top state if it exists
    if (!m_activeStates.empty()) {
      m_activeStates.back()->pause();
    }

    // Push the new state onto the stack and enter it
    m_activeStates.push_back(it->second);
    m_activeStates.back()->enter();
    GAMESTATE_INFO("Pushed state: " + stateName);
  } else {
    GAMESTATE_ERROR("State not found: " + stateName);
  }
}

void GameStateManager::popState() {
  if (!m_activeStates.empty()) {
    // Exit and pop the current state
    m_activeStates.back()->exit();
    m_activeStates.pop_back();
    GAMESTATE_INFO("Popped state");

    // Resume the new top state if it exists
    if (!m_activeStates.empty()) {
      m_activeStates.back()->resume();
    }
  }
}

void GameStateManager::changeState(const std::string &stateName) {
  // Pop the current state if one exists
  if (!m_activeStates.empty()) {
    popState();
  }
  // Push the new state
  pushState(stateName);
}

void GameStateManager::update(float deltaTime) {
  m_lastDeltaTime = deltaTime; // Store deltaTime for render
  // Update all active states on the stack
  for (const auto &state : m_activeStates) {
    state->update(deltaTime);
  }
}

void GameStateManager::render() {
  // Render all active states on the stack
  for (const auto &state : m_activeStates) {
    state->render(m_lastDeltaTime);
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
