/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef PLAYER_RUNNING_STATE_HPP
#define PLAYER_RUNNING_STATE_HPP

#include "entities/states/EntityState.hpp"

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
