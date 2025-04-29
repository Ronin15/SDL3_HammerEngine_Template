/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "EntityStateManager.hpp"
#include "EntityState.hpp"
#include <iostream>
#include <stdexcept>

EntityStateManager::EntityStateManager() : currentState(nullptr) {}

EntityStateManager::~EntityStateManager() {
  currentState = nullptr;
}

void EntityStateManager::addState(const std::string& stateName, std::unique_ptr<EntityState> state) {
  if (states.find(stateName) != states.end()) {
    throw std::invalid_argument("Forge Game Engine - State already exists" + stateName);
  }
  states[stateName] = std::move(state);
}

void EntityStateManager::setState(const std::string& stateName) {
  if (currentState) {
    currentState->exit();
  }
  // find new state
  auto it = states.find(stateName);
  if (it != states.end()) {
    // set current state and enter
    currentState = it->second.get();
    currentState->enter();
  } else {
    // state not found set to null
    std::cerr << "Forge Game Engine - State not found: " << stateName << std::endl;
    currentState = nullptr;
  }
}

std::string EntityStateManager::getCurrentStateName() const {
  for (const auto& pair : states) {
    if (pair.second.get() == currentState) {
      return pair.first;
    }
  }
  return "";
}

bool EntityStateManager::hasState(const std::string& stateName) const {
  return states.find(stateName) != states.end();
}

// TODO: REMOVE THIS FUNCTION later as Entity states will be set and not
// deleted/removed.
void EntityStateManager::removeState(const std::string& stateName) {
  // cehck if current state is being removed
  if (currentState) {
    auto it = states.find(stateName);
    if (it != states.end() && it->second.get() == currentState) {
      currentState = nullptr;
    }
  }
  // remove state
  states.erase(stateName);
}

void EntityStateManager::update() {
  if (currentState) {
    currentState->update();
  }
}
