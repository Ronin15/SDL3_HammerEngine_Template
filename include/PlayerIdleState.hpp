/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef PLAYER_IDLE_STATE_HPP
#define PLAYER_IDLE_STATE_HPP

#include "EntityState.hpp"

class Player;

class PlayerIdleState : public EntityState {
public:
    PlayerIdleState(Player* player);

    void enter() override;
    void update() override;
    void exit() override;

private:
    Player* mp_player;
};

#endif  // PLAYER_IDLE_STATE_HPP
