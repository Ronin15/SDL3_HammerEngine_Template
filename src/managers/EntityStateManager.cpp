/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/EntityStateManager.hpp"
#include "entities/EntityState.hpp"
#include "core/Logger.hpp"
#include <stdexcept>
#include <algorithm>

EntityStateManager::EntityStateManager() : currentState() {}

EntityStateManager::~EntityStateManager() {
  currentState.reset();
}

void EntityStateManager::addState(const std::string& stateName, std::unique_ptr<EntityState> state) {
  if (states.find(stateName) != states.end()) {
    ENTITYSTATE_ERROR("State already exists: " + stateName);
    throw std::invalid_argument("Hammer Game Engine - State already exists" + stateName);
  }
  // Convert unique_ptr to shared_ptr and add to container
  states[stateName] = std::shared_ptr<EntityState>(state.release());
}

void EntityStateManager::setState(const std::string& stateName) {
  if (auto current = currentState.lock()) {
    current->exit();
  }
  // find new state
  auto it = states.find(stateName);
  if (it != states.end()) {
    // set current state and enter (non-owning observer via weak_ptr)
    currentState = it->second;
    if (auto current = currentState.lock()) {
      current->enter();
    }
  } else {
    // state not found, reset weak_ptr
    ENTITYSTATE_ERROR("State not found: " + stateName);
    currentState.reset();
  }
}

std::string EntityStateManager::getCurrentStateName() const {
  if (auto current = currentState.lock()) {
    auto it = std::find_if(states.begin(), states.end(),
                          [&current](const auto& pair) {
                            return pair.second == current;
                          });
    return (it != states.end()) ? it->first : "";
  }
  return "";
}

bool EntityStateManager::hasState(const std::string& stateName) const {
  return states.find(stateName) != states.end();
}

// TODO: REMOVE THIS FUNCTION later as Entity states will be set and not
// deleted/removed.
void EntityStateManager::removeState(const std::string& stateName) {
  // check if current state is being removed
  if (auto current = currentState.lock()) {
    auto it = states.find(stateName);
    if (it != states.end() && it->second == current) {
      currentState.reset();
    }
  }
  // remove state
  states.erase(stateName);
}

void EntityStateManager::update(float deltaTime) {
  if (auto current = currentState.lock()) {
    current->update(deltaTime);
  }
}
