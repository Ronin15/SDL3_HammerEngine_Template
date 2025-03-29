#include "GamePlayState.hpp"
#include <iostream>

void GamePlayState::enter() { std::cout << "Entering GAME State" << std::endl; }

void GamePlayState::update() {
  std::cout << "Updating GAME State" << std::endl;
}

void GamePlayState::render() {
  std::cout << "Rendering GAME State" << std::endl;
}
void GamePlayState::exit() { std::cout << "Exiting GAME State" << std::endl; }

std::string GamePlayState::getName() const { return "GamePlayState"; }
