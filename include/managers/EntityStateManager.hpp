/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef ENTITY_STATE_MANAGER_HPP
#define ENTITY_STATE_MANAGER_HPP

#include <memory>
#include <string>
#include <boost/container/flat_map.hpp>
#include "entities/EntityState.hpp"

class EntityStateManager {

 public:
  EntityStateManager();
  void addState(const std::string& stateName, std::unique_ptr<EntityState> state);
  void setState(const std::string& stateName);
  std::string getCurrentStateName() const;
  bool hasState(const std::string& stateName) const;
  void removeState(const std::string& stateName);
  void update(float deltaTime);
  ~EntityStateManager();

  private:
   boost::container::flat_map<std::string, std::unique_ptr<EntityState>> states;
   // Non-owning pointer to the current active state
   // This state is owned by the 'states' container above
   EntityState* currentState{nullptr};
};

#endif  // ENTITY_STATE_MANAGER_HPP
