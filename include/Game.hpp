#ifndef Game_hpp
#define Game_hpp

#include <SDL3/SDL.h>
#include <string>

class Game{
  public:
    static Game* Instance(){
      if(pInstance_ == nullptr){
        pInstance_ = new Game();
      }
      return pInstance_;
    }

    bool Init(int SWIDTH, int SHEIGHT, std::string title);
    void Handle_Events();
    void Update();
    void Draw();
    void Clean();

  private:
    static Game* pInstance_;
    const int SWIDTH{0};
    const int SHEIGHT{0};
    std::string title{0};
    
};

#endif //Game_hpp
 
