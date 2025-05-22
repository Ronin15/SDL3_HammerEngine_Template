/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef PLAYER_IDLE_STATE_HPP
#define PLAYER_IDLE_STATE_HPP

#include "entities/EntityState.hpp"

class Player;

class PlayerIdleState : public EntityState {
public:
    PlayerIdleState(Player* player);

    void enter() override;
    void update() override;
    void exit() override;

private:
    // Non-owning pointer to the player entity
    // The player entity is owned elsewhere in the application
    Player* mp_player{nullptr};
};

#endif  // PLAYER_IDLE_STATE_HPP
