#ifndef MAIN_MENU_STATE_HPP
#define MAIN_MENU_STATE_HPP

#include "GameState.hpp"

class MainMenuState : public GameState {
   public:
    void enter() override;
    void handleInput() override;
    void update() override;
    void exit() override;
    std::string getName() const override;
};

#endif //MAIN_MENU_STATE_HPP

