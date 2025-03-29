#include "EntityRunningState.hpp"
#include <iostream>

void EntityRunningState::enter() {
  std::cout << "Entering Player Running State" << std::endl;
}

void EntityRunningState::update() {
  std::cout << "Updating Player Running State" << std::endl;
}

void EntityRunningState::render() {
  std::cout << "Rendering Player Running State" << std::endl;
}

void EntityRunningState::exit() {
  std::cout << "Exiting Player Running State" << std::endl;
}
