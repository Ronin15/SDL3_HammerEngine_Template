/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef NPC_ATTACKING_STATE_HPP
#define NPC_ATTACKING_STATE_HPP

#include "entities/EntityState.hpp"
#include <functional>

class NPC;

class NPCAttackingState : public EntityState {
public:
    explicit NPCAttackingState(NPC& npc);

    void enter() override;
    void update(float deltaTime) override;
    void exit() override;

private:
    // Non-owning reference to the NPC entity
    // The NPC entity is owned elsewhere in the application
    std::reference_wrapper<NPC> m_npc;
    float m_animationDuration{0.0f};
    float m_elapsedTime{0.0f};
};

#endif // NPC_ATTACKING_STATE_HPP
