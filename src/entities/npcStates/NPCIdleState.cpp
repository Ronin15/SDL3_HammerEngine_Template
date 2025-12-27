/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/npcStates/NPCIdleState.hpp"
#include "entities/NPC.hpp"

NPCIdleState::NPCIdleState(NPC& npc) : m_npc(npc) {}

void NPCIdleState::enter() {
    // Play idle animation using abstracted API
    m_npc.get().playAnimation("idle");
}

void NPCIdleState::update(float deltaTime) {
    (void)deltaTime;
    // Animation frame updates are handled in NPC::update()
    // State transitions are driven externally by behaviors
}

void NPCIdleState::exit() {
    // Nothing special needed on exit
}
