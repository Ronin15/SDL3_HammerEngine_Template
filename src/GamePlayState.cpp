#include "GamePlayState.hpp"
#include <iostream>

void GamePlayState::enter() {
    std::cout << "Entering GAME State" << std::endl;
}

void GamePlayState::handleInput() {
    std::cout << "Handling GAME Input" << std::endl;
}

void GamePlayState::update() {
    std::cout << "Updating GAME State" << std::endl;
}

void GamePlayState::exit() {
    std::cout << "Exiting GAME State" << std::endl;
}

std::string GamePlayState::getName() const {
    return "GamePlayState";
}
