#include "PauseState.hpp"
#include <iostream>

void PauseState::enter() {
    std::cout << "Entering PAUSE State" << std::endl;
}

void PauseState::handleInput() {
    std::cout << "Handling PAUSE Input" << std::endl;
}

void PauseState::update() {
    std::cout << "Updating PAUSE State" << std::endl;
}

void PauseState::exit() {
    std::cout << "Exiting PAUSE State" << std::endl;
}

std::string PauseState::getName() const {
    return "PauseState";
}
