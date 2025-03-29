#include "EntityIdleState.hpp"
#include <iostream>

void EntityIdleState::enter() {
  std::cout << "Entering Player Idle State" << std::endl;
}

void EntityIdleState::update() {
  std::cout << "Updating Player Idle State" << std::endl;
}

void EntityIdleState::render() {
  std::cout << "Rendering Player Idle State" << std::endl;
}

void EntityIdleState::exit() {
  std::cout << "Exiting Player Idle State" << std::endl;
}
