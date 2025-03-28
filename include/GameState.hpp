#ifndef GAME_STATE_HPP
#define GAME_STATE_HPP

#include <string>
//pure virtual for inheritance

class GameState {
  public:
    virtual void enter() = 0;
    virtual void handleInput() = 0;
    virtual void update() = 0;
    virtual void exit() = 0;
    virtual std::string getName() const = 0;
    virtual ~GameState() = default;
};
#endif //GAME_STATE_HPP


