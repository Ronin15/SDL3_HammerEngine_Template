/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef COMBAT_EVENT_HPP
#define COMBAT_EVENT_HPP

/**
 * @file CombatEvent.hpp
 * @brief Combat event implementation for damage, attacks, and combat state changes
 *
 * CombatEvent allows the game to notify systems of combat occurrences:
 * - Player attacks
 * - NPC damage taken
 * - Entity deaths
 * - Combat state transitions
 */

#include "Event.hpp"
#include <string>

// Forward declarations
class Entity;

enum class CombatEventType {
    PlayerAttacked,    // Player initiated an attack
    NPCDamaged,        // NPC took damage (includes attacker, target, damage amount)
    NPCKilled,         // NPC died
    PlayerDamaged,     // Player took damage
    PlayerKilled       // Player died
};

class CombatEvent : public Event {
public:
    explicit CombatEvent(CombatEventType combatType, Entity* attacker = nullptr,
                         Entity* target = nullptr, float damage = 0.0f);
    ~CombatEvent() override = default;

    // Core event methods implementation
    void update() override;
    void execute() override;
    void reset() override;
    void clean() override;

    // Event identification
    std::string getName() const override { return m_name; }
    std::string getType() const override { return "Combat"; }
    std::string getTypeName() const override { return "CombatEvent"; }
    EventTypeId getTypeId() const override { return EventTypeId::Combat; }

    // Condition checking (always true for combat events - they're immediate)
    bool checkConditions() override { return true; }

    // Combat-specific accessors
    [[nodiscard]] CombatEventType getCombatType() const { return m_combatType; }
    [[nodiscard]] Entity* getAttacker() const { return m_attacker; }
    [[nodiscard]] Entity* getTarget() const { return m_target; }
    [[nodiscard]] float getDamage() const { return m_damage; }
    [[nodiscard]] float getRemainingHealth() const { return m_remainingHealth; }

    // Combat-specific setters
    void setRemainingHealth(float health) { m_remainingHealth = health; }

    // Utility
    [[nodiscard]] std::string getCombatTypeString() const;

private:
    std::string m_name;
    CombatEventType m_combatType;
    Entity* m_attacker{nullptr};   // Raw ptr, doesn't own
    Entity* m_target{nullptr};     // Raw ptr, doesn't own
    float m_damage{0.0f};
    float m_remainingHealth{0.0f};
};

#endif // COMBAT_EVENT_HPP
