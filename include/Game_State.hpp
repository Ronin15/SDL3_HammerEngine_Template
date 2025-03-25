#ifndef Game_State_hpp
#define Game_State_hpp

#include<memory>

class Game_State{
  public:
    virtual void update() = 0;
    virtual void render() = 0;
    virtual bool is_Complete() const = 0;
    virtual std::unique_ptr<Game_State> getNextState() = 0;
    virtual ~Game_State() = default;
};




#endif //Game_State_Hpp
