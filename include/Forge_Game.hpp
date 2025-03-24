#ifndef Forge_Game_hpp
#define Forge_Game_hpp

#include <SDL3/SDL.h>
#include <string>

class Forge_Game{
  public:
    static Forge_Game* Instance(){
      if(pInstance_ == nullptr){
        pInstance_ = new Forge_Game();
      }
      return pInstance_;
    }

    bool Init(int SWIDTH, int SHEIGHT, std::string title);
    void Handle_Events();
    void Update();
    void Draw();
    void Clean();

  private:
    static Forge_Game* pInstance_;
    const int SWIDTH{0};
    const int SHEIGHT{0};
    std::string title{0};
    
};

#endif //Forge_Game_hpp
 
