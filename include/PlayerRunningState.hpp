#ifndef PLAYER_RUNNING_STATE_HPP
#define PLAYER_RUNNING_STATE_HPP

#include "EntityState.hpp"

class Player;

class PlayerRunningState : public EntityState {
public:
    PlayerRunningState(Player* player);

    void enter() override;
    void update() override;
    void exit() override;

private:
    Player* mp_player;
};

#endif  // PLAYER_RUNNING_STATE_HPP
