/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "events/CombatEvent.hpp"
#include "core/Logger.hpp"
#include <format>

CombatEvent::CombatEvent(CombatEventType combatType, Entity* attacker,
                         Entity* target, float damage)
    : m_combatType(combatType)
    , m_attacker(attacker)
    , m_target(target)
    , m_damage(damage)
{
    // Generate name based on combat type
    m_name = std::format("CombatEvent_{}", getCombatTypeString());
}

void CombatEvent::update() {
    // Combat events are immediate - no update logic needed
}

void CombatEvent::execute() {
    // Combat events are data carriers dispatched through EventManager
    // The actual combat logic is in CombatController
    COMBAT_DEBUG(std::format("Combat event executed: {} damage={:.1f}",
        getCombatTypeString(), m_damage));
}

void CombatEvent::reset() {
    m_damage = 0.0f;
    m_remainingHealth = 0.0f;
    m_attacker = nullptr;
    m_target = nullptr;
}

void CombatEvent::clean() {
    reset();
}

std::string CombatEvent::getCombatTypeString() const {
    switch (m_combatType) {
        case CombatEventType::PlayerAttacked:
            return "PlayerAttacked";
        case CombatEventType::NPCDamaged:
            return "NPCDamaged";
        case CombatEventType::NPCKilled:
            return "NPCKilled";
        case CombatEventType::PlayerDamaged:
            return "PlayerDamaged";
        case CombatEventType::PlayerKilled:
            return "PlayerKilled";
        default:
            return "Unknown";
    }
}
