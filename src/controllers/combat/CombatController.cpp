/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/combat/CombatController.hpp"
#include "entities/Player.hpp"
#include "entities/NPC.hpp"
#include "events/CombatEvent.hpp"
#include "managers/EventManager.hpp"
#include "managers/AIManager.hpp"
#include "core/Logger.hpp"
#include <format>
#include <cmath>

void CombatController::subscribe() {
    if (checkAlreadySubscribed()) {
        return;
    }

    // CombatController doesn't need to subscribe to any events currently
    // It drives combat, rather than reacting to events
    // Future: could subscribe to damage events from other sources

    setSubscribed(true);
    COMBAT_INFO("CombatController subscribed");
}

void CombatController::update(float deltaTime, Player& player) {
    // Update attack cooldown
    if (m_attackCooldown > 0.0f) {
        m_attackCooldown -= deltaTime;
        if (m_attackCooldown < 0.0f) {
            m_attackCooldown = 0.0f;
        }
    }

    // Regenerate stamina when not attacking
    if (m_attackCooldown <= 0.0f) {
        regenerateStamina(player, deltaTime);
    }

    // Update target display timer
    updateTargetTimer(deltaTime);
}

bool CombatController::tryAttack(Player& player) {
    // Check cooldown
    if (m_attackCooldown > 0.0f) {
        COMBAT_DEBUG(std::format("Attack on cooldown: {:.2f}s remaining", m_attackCooldown));
        return false;
    }

    // Check stamina
    if (!player.canAttack(ATTACK_STAMINA_COST)) {
        COMBAT_DEBUG(std::format("Not enough stamina to attack. Need {:.1f}, have {:.1f}",
            ATTACK_STAMINA_COST, player.getStamina()));
        return false;
    }

    // Consume stamina and start cooldown
    float oldStamina = player.getStamina();
    player.consumeStamina(ATTACK_STAMINA_COST);
    m_attackCooldown = ATTACK_COOLDOWN;

    COMBAT_INFO(std::format("Player attacking! Stamina: {:.1f} -> {:.1f}",
        oldStamina, player.getStamina()));

    // Transition player to attacking state
    player.changeState("attacking");

    // Perform hit detection using AIManager
    performAttack(player);

    // Dispatch player attacked event
    auto attackEvent = std::make_shared<CombatEvent>(
        CombatEventType::PlayerAttacked, &player, nullptr, player.getAttackDamage());
    EventManager::Instance().dispatchEvent(attackEvent, EventManager::DispatchMode::Immediate);

    return true;
}

void CombatController::performAttack(Player& player) {
    const Vector2D playerPos = player.getPosition();
    const float attackRange = player.getAttackRange();
    const float attackDamage = player.getAttackDamage();

    // Determine attack direction based on player facing
    float attackDirX = (player.getFlip() == SDL_FLIP_HORIZONTAL) ? -1.0f : 1.0f;

    // Query nearby entities from AIManager
    std::vector<EntityPtr> nearbyEntities;
    AIManager::Instance().queryEntitiesInRadius(playerPos, attackRange, nearbyEntities, true);

    // Check all nearby entities for hits
    std::shared_ptr<NPC> closestHit = nullptr;
    float closestDist = attackRange + 1.0f;

    for (const auto& entityPtr : nearbyEntities) {
        // Try to cast to NPC (skip non-NPC entities)
        auto npc = std::dynamic_pointer_cast<NPC>(entityPtr);
        if (!npc || !npc->isAlive()) {
            continue;
        }

        const Vector2D npcPos = npc->getPosition();
        const Vector2D diff = npcPos - playerPos;
        const float distance = diff.length();

        // Check if in attack direction (180 degree arc in front of player)
        // Normalize direction to player facing
        float dotProduct = diff.getX() * attackDirX;
        if (dotProduct < 0.0f) {
            // NPC is behind the player
            continue;
        }

        // Hit detected
        float oldHealth = npc->getHealth();

        // Calculate knockback direction
        Vector2D knockback = diff.normalized() * 20.0f;
        npc->takeDamage(attackDamage, knockback);

        COMBAT_INFO(std::format("Hit {} for {:.1f} damage! HP: {:.1f} -> {:.1f}",
            npc->getName(), attackDamage, oldHealth, npc->getHealth()));

        // Track closest hit for targeting
        if (distance < closestDist) {
            closestDist = distance;
            closestHit = npc;
        }

        // Dispatch NPC damaged event
        auto damageEvent = std::make_shared<CombatEvent>(
            CombatEventType::NPCDamaged, &player, npc.get(), attackDamage);
        damageEvent->setRemainingHealth(npc->getHealth());
        EventManager::Instance().dispatchEvent(damageEvent, EventManager::DispatchMode::Immediate);

        // Check for kill
        if (!npc->isAlive()) {
            COMBAT_INFO(std::format("{} killed!", npc->getName()));

            auto killEvent = std::make_shared<CombatEvent>(
                CombatEventType::NPCKilled, &player, npc.get(), attackDamage);
            EventManager::Instance().dispatchEvent(killEvent, EventManager::DispatchMode::Immediate);
        }
    }

    // Update target tracking
    if (closestHit) {
        m_targetedNPC = closestHit;
        m_targetDisplayTimer = TARGET_DISPLAY_DURATION;
    }
}

void CombatController::regenerateStamina(Player& player, float deltaTime) {
    float currentStamina = player.getStamina();
    float maxStamina = player.getMaxStamina();

    if (currentStamina < maxStamina) {
        float regenAmount = STAMINA_REGEN_RATE * deltaTime;
        player.restoreStamina(regenAmount);
    }
}

void CombatController::updateTargetTimer(float deltaTime) {
    if (m_targetDisplayTimer > 0.0f) {
        m_targetDisplayTimer -= deltaTime;
        if (m_targetDisplayTimer <= 0.0f) {
            m_targetDisplayTimer = 0.0f;
            m_targetedNPC.reset();
            COMBAT_DEBUG("Target display timer expired");
        }
    }
}

std::shared_ptr<NPC> CombatController::getTargetedNPC() const {
    // Safely lock weak_ptr and verify target is still valid (alive)
    auto target = m_targetedNPC.lock();
    if (!target || !target->isAlive()) {
        return nullptr;
    }
    return target;
}

bool CombatController::hasActiveTarget() const {
    return m_targetDisplayTimer > 0.0f && getTargetedNPC() != nullptr;
}
