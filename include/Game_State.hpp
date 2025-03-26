#ifndef Game_State_hpp
#define Game_State_hpp

#include<memory>

class Game_State{
  public:
    virtual void Update() = 0;
    virtual void Render() = 0;
    virtual bool IsComplete() const = 0;
    virtual std::unique_ptr<Game_State> getNextState() = 0;
    virtual ~Game_State() = default;
};




#endif //Game_State_Hpp
