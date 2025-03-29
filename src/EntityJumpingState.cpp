#include "EntityJumpingState.hpp"
#include <iostream>

void EntityJumpingState::enter() {
  std::cout << "Entering Player Jumping State" << std::endl;
}

void EntityJumpingState::update() {
  std::cout << "Updating Player Jumping State" << std::endl;
}

void EntityJumpingState::render() {
  std::cout << "Rendering Player Jumping State" << std::endl;
}

void EntityJumpingState::exit() {
  std::cout << "Exiting Player Jumping State" << std::endl;
}
