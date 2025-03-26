#ifndef MENU_STATE
#define MENU_STATE

#include "Game_State.hpp"

class Menu_State : public Game_State {
  public:
    void Update() override;
    void Render() override;
    bool IsComplete() const override;
    std::unique_ptr<Game_State> getNextState() override;

  private:
    int score{0};
    bool gameOver{false}; 
};

#endif //MENU_STATE
