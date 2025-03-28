#include "MainMenuState.hpp"
#include <iostream>

void MainMenuState::enter() {
    std::cout << "Entering Main Menu State" << std::endl;
}

void MainMenuState::update() {
    std::cout << "Updating Main Menu State" << std::endl;
}

void MainMenuState::render() {
    std::cout << "Rendering Main Menu State" << std::endl;
}
void MainMenuState::exit() {
    std::cout << "Exiting Main Menu State" << std::endl;
}

std::string MainMenuState::getName() const {
    return "MainMenuState";
}
