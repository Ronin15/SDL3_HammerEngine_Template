/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/npcStates/NPCAttackingState.hpp"
#include "entities/NPC.hpp"

NPCAttackingState::NPCAttackingState(NPC& npc) : m_npc(npc) {}

void NPCAttackingState::enter() {
    // Play attacking animation using abstracted API
    // This animation plays once (non-looping)
    m_npc.get().playAnimation("attacking");
}

void NPCAttackingState::update(float deltaTime) {
    (void)deltaTime;
    // Animation frame updates are handled in NPC::update()
    // Attack completion triggers are handled by AttackBehavior
}

void NPCAttackingState::exit() {
    // Nothing special needed on exit
}
