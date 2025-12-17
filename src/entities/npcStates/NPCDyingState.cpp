/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/npcStates/NPCDyingState.hpp"
#include "entities/NPC.hpp"

NPCDyingState::NPCDyingState(NPC& npc) : m_npc(npc) {}

void NPCDyingState::enter() {
    // Play dying animation using abstracted API
    // This animation plays once (non-looping)
    m_npc.get().playAnimation("dying");

    // Stop all movement
    m_npc.get().setVelocity(Vector2D(0, 0));
}

void NPCDyingState::update(float deltaTime) {
    (void)deltaTime;
    // Animation frame updates are handled in NPC::update()
    // Death animation completion could trigger cleanup/loot drop
}

void NPCDyingState::exit() {
    // Nothing special needed on exit
    // NPC cleanup is handled externally
}
