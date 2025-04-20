#ifndef PLAYER_HPP
#define PLAYER_HPP

#include "Entity.hpp"
#include "Vector2D.hpp"
#include <_printf.h>
class Player : public Entity{
public:
    Player();
    ~Player();

    void update()override;
    void render()override;
    void clean()override;

private:
    void handleInput();

};

#endif // PLAYER_HPP
