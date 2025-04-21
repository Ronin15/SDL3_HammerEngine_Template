#ifndef PLAYER_IDLE_STATE_HPP
#define PLAYER_IDLE_STATE_HPP

#include "EntityState.hpp"

class Player;

class PlayerIdleState : public EntityState {
public:
    PlayerIdleState(Player* player);
    
    void enter() override;
    void update() override;
    void render() override;
    void exit() override;

private:
    Player* m_player;
};

#endif  // PLAYER_IDLE_STATE_HPP