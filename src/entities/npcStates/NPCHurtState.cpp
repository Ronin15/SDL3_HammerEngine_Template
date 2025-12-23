/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/npcStates/NPCHurtState.hpp"
#include "entities/NPC.hpp"

NPCHurtState::NPCHurtState(NPC& npc) : m_npc(npc) {}

void NPCHurtState::enter() {
    m_npc.get().playAnimation("hurt");
    m_animationDuration = static_cast<float>(m_npc.get().getNumFrames() * m_npc.get().getAnimSpeed()) / 1000.0f;
    m_elapsedTime = 0.0f;
    m_npc.get().setVelocity(Vector2D(0, 0));
}

void NPCHurtState::update(float deltaTime) {
    m_elapsedTime += deltaTime;
    if (m_elapsedTime >= m_animationDuration) {
        m_npc.get().setAnimationState("Idle");
    }
}

void NPCHurtState::exit() {
    // Nothing special needed on exit
}
