/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/npcStates/NPCHurtState.hpp"
#include "entities/NPC.hpp"

NPCHurtState::NPCHurtState(NPC& npc) : m_npc(npc) {}

void NPCHurtState::enter() {
    // Play hurt animation using abstracted API
    // This animation plays once (non-looping)
    m_npc.get().playAnimation("hurt");
}

void NPCHurtState::update(float deltaTime) {
    (void)deltaTime;
    // Animation frame updates are handled in NPC::update()
    // Hurt reaction completion will trigger transition back to previous state
}

void NPCHurtState::exit() {
    // Nothing special needed on exit
}
