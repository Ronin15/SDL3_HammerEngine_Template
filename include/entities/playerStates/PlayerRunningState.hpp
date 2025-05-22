/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef PLAYER_RUNNING_STATE_HPP
#define PLAYER_RUNNING_STATE_HPP

#include "entities/EntityState.hpp"

class Player;

class PlayerRunningState : public EntityState {
public:
    PlayerRunningState(Player* player);

    void enter() override;
    void update() override;
    void exit() override;

private:
    // Non-owning pointer to the player entity
    // The player entity is owned elsewhere in the application
    Player* mp_player{nullptr};
};

#endif  // PLAYER_RUNNING_STATE_HPP
