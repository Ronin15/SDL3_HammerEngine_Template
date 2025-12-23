/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/npcStates/NPCRecoveringState.hpp"
#include "entities/NPC.hpp"

NPCRecoveringState::NPCRecoveringState(NPC& npc) : m_npc(npc) {}

void NPCRecoveringState::enter() {
    m_npc.get().playAnimation("recovering");
    m_animationDuration = static_cast<float>(m_npc.get().getNumFrames() * m_npc.get().getAnimSpeed()) / 1000.0f;
    m_elapsedTime = 0.0f;
    m_npc.get().setVelocity(Vector2D(0, 0));
}

void NPCRecoveringState::update(float deltaTime) {
    m_elapsedTime += deltaTime;
    if (m_elapsedTime >= m_animationDuration) {
        m_npc.get().setAnimationState("Idle");
    }
}

void NPCRecoveringState::exit() {
    // Nothing special needed on exit
}
