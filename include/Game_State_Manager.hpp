#ifndef GAME_STATE_MANAGER_HPP
#define GAME_STATE_MANAGER_HPP

#include "Game_State.hpp"
#include <vector>

class Game_State_Manager {
  public:
    void run(std::unique_ptr<Game_State> initialState);
    bool is_Running() const {return !states.empty();}

  private:
    std::vector<std::unique_ptr<Game_State>> states;
};


#endif //GAME_STATE_MANAGER_HPP
