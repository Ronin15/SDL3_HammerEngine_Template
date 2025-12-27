/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef NPC_IDLE_STATE_HPP
#define NPC_IDLE_STATE_HPP

#include "entities/EntityState.hpp"
#include <functional>

class NPC;

class NPCIdleState : public EntityState {
public:
    explicit NPCIdleState(NPC& npc);

    void enter() override;
    void update(float deltaTime) override;
    void exit() override;

private:
    // Non-owning reference to the NPC entity
    // The NPC entity is owned elsewhere in the application
    std::reference_wrapper<NPC> m_npc;
};

#endif // NPC_IDLE_STATE_HPP
