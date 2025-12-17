/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/npcStates/NPCRecoveringState.hpp"
#include "entities/NPC.hpp"

NPCRecoveringState::NPCRecoveringState(NPC& npc) : m_npc(npc) {}

void NPCRecoveringState::enter() {
    // Play recovering animation using abstracted API
    // This animation plays once (non-looping)
    m_npc.get().playAnimation("recovering");
}

void NPCRecoveringState::update(float deltaTime) {
    (void)deltaTime;
    // Animation frame updates are handled in NPC::update()
    // Recovery completion is handled by AttackBehavior timing
}

void NPCRecoveringState::exit() {
    // Nothing special needed on exit
}
