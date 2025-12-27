/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/npcStates/NPCWalkingState.hpp"
#include "entities/NPC.hpp"

NPCWalkingState::NPCWalkingState(NPC& npc) : m_npc(npc) {}

void NPCWalkingState::enter() {
    // Play walking animation using abstracted API
    m_npc.get().playAnimation("walking");
}

void NPCWalkingState::update(float deltaTime) {
    (void)deltaTime;
    // Animation frame updates are handled in NPC::update()
    // State transitions are driven externally by behaviors
}

void NPCWalkingState::exit() {
    // Nothing special needed on exit
}
