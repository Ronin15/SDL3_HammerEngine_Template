#include "EntityWalkingState.hpp"
#include <iostream>

void EntityWalkingState::enter() {
  std::cout << "Entering Player Walking State" << std::endl;
}

void EntityWalkingState::update() {
  std::cout << "Updating Player Walking State" << std::endl;
}

void EntityWalkingState::render() {
  std::cout << "Rendering Player Walking State" << std::endl;
}

void EntityWalkingState::exit() {
  std::cout << "Exiting Player Walking State" << std::endl;
}
