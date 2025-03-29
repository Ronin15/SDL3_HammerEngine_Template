#include "PauseState.hpp"
#include <iostream>

void PauseState::enter() {
  std::cout << "Entering PAUSE State" << std::endl;
}

void PauseState::update() {
  std::cout << "Updating PAUSE State" << std::endl;
}

void PauseState::render() {
  std::cout << "Rendering PAUSE State" << std::endl;
}
void PauseState::exit() {
  std::cout << "Exiting PAUSE State" << std::endl;
}

std::string PauseState::getName() const {
  return "PauseState";
}
