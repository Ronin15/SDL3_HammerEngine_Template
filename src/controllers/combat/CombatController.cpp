/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/combat/CombatController.hpp"
#include "core/Logger.hpp"
#include "entities/NPC.hpp"
#include "entities/Player.hpp"
#include "events/CombatEvent.hpp"
#include "events/EntityEvents.hpp"
#include "managers/AIManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include <cmath>
#include <format>

void CombatController::subscribe()
{
    if (checkAlreadySubscribed()) {
        return;
    }

    // CombatController doesn't need to subscribe to any events currently
    // It drives combat, rather than reacting to events
    // Future: could subscribe to damage events from other sources

    setSubscribed(true);
    COMBAT_INFO("CombatController subscribed");
}

void CombatController::update(float deltaTime)
{
    auto player = mp_player.lock();
    if (!player) {
        return;
    }

    // Update attack cooldown
    if (m_attackCooldown > 0.0f) {
        m_attackCooldown -= deltaTime;
        if (m_attackCooldown < 0.0f) {
            m_attackCooldown = 0.0f;
        }
    }

    // Regenerate stamina when not attacking
    if (m_attackCooldown <= 0.0f) {
        regenerateStamina(player.get(), deltaTime);
    }

    // Update target display timer
    updateTargetTimer(deltaTime);
}

bool CombatController::tryAttack()
{
    auto player = mp_player.lock();
    if (!player) {
        return false;
    }

    // Check cooldown
    if (m_attackCooldown > 0.0f) {
        COMBAT_DEBUG(std::format("Attack on cooldown: {:.2f}s remaining", m_attackCooldown));
        return false;
    }

    // Check stamina
    if (!player->canAttack(ATTACK_STAMINA_COST)) {
        COMBAT_DEBUG(std::format("Not enough stamina to attack. Need {:.1f}, have {:.1f}",
            ATTACK_STAMINA_COST, player->getStamina()));
        return false;
    }

    // Consume stamina and start cooldown
    float oldStamina = player->getStamina();
    player->consumeStamina(ATTACK_STAMINA_COST);
    m_attackCooldown = ATTACK_COOLDOWN;

    COMBAT_INFO(std::format("Player attacking! Stamina: {:.1f} -> {:.1f}",
        oldStamina, player->getStamina()));

    // Transition player to attacking state
    player->changeState("attacking");

    // Perform hit detection using AIManager
    performAttack(player.get());

    // Dispatch player attacked event
    auto attackEvent = std::make_shared<CombatEvent>(
        CombatEventType::PlayerAttacked, player.get(), nullptr, player->getAttackDamage());
    EventManager::Instance().dispatchEvent(attackEvent, EventManager::DispatchMode::Immediate);

    return true;
}

void CombatController::performAttack(Player* player)
{
    if (!player) {
        return;
    }

    const Vector2D playerPos = player->getPosition();
    const float attackRange = player->getAttackRange();
    const float attackDamage = player->getAttackDamage();

    // Determine attack direction based on player facing
    float attackDirX = (player->getFlip() == SDL_FLIP_HORIZONTAL) ? -1.0f : 1.0f;

    // Query nearby entities from AIManager (read-only query)
    std::vector<EntityPtr> nearbyEntities;
    AIManager::Instance().queryEntitiesInRadius(playerPos, attackRange, nearbyEntities, true);

    // Check all nearby entities for hits
    std::shared_ptr<NPC> closestHit = nullptr;
    float closestDist = attackRange + 1.0f;

    // Get player's EntityHandle for event-driven damage
    EntityHandle playerHandle = player->getHandle();

    for (const auto& entityPtr : nearbyEntities) {
        // Use EntityKind for fast type check (no RTTI overhead)
        if (entityPtr->getKind() != EntityKind::NPC) {
            continue;
        }

        // Safe static_cast - we verified the kind via enum
        auto* npc = static_cast<NPC*>(entityPtr.get());
        if (!npc->isAlive()) {
            continue;
        }

        const Vector2D npcPos = npc->getPosition();
        const Vector2D diff = npcPos - playerPos;
        const float distance = diff.length();

        // Check if in attack direction (180 degree arc in front of player)
        float dotProduct = diff.getX() * attackDirX;
        if (dotProduct < 0.0f) {
            // NPC is behind the player
            continue;
        }

        // Hit detected - calculate knockback direction
        Vector2D knockback = diff.normalized() * 20.0f;
        float oldHealth = npc->getHealth();

        // === EVENT-DRIVEN DAMAGE PATTERN ===
        // Fire DamageIntent event with EntityHandle (new architecture)
        // This allows handlers to process damage centrally via EntityDataManager
        EntityHandle targetHandle = npc->getHandle();
        auto damageIntent = std::make_shared<DamageEvent>(
            EntityEventType::DamageIntent, playerHandle, targetHandle,
            attackDamage, knockback);
        EventManager::Instance().dispatchEvent(damageIntent, EventManager::DispatchMode::Immediate);

        // Bridge: Also call existing method until EntityDataManager health migration is complete
        // TODO: Remove this when DamageHandler processes via EntityDataManager
        npc->takeDamage(attackDamage, knockback);

        COMBAT_INFO(std::format("Hit {} for {:.1f} damage! HP: {:.1f} -> {:.1f}",
            npc->getName(), attackDamage, oldHealth, npc->getHealth()));

        // Track closest hit for targeting
        if (distance < closestDist) {
            closestDist = distance;
            closestHit = std::static_pointer_cast<NPC>(entityPtr);
        }

        // Fire CombatEvent for observers (UI, sound, etc.)
        auto damageEvent = std::make_shared<CombatEvent>(
            CombatEventType::NPCDamaged, player, npc, attackDamage);
        damageEvent->setRemainingHealth(npc->getHealth());
        EventManager::Instance().dispatchEvent(damageEvent, EventManager::DispatchMode::Immediate);

        // Check for kill
        if (!npc->isAlive()) {
            COMBAT_INFO(std::format("{} killed!", npc->getName()));

            // Fire DeathEvent with EntityHandle (new architecture)
            auto deathEvent = std::make_shared<DeathEvent>(
                EntityEventType::DeathCompleted, targetHandle, playerHandle);
            deathEvent->setDeathPosition(npcPos);
            EventManager::Instance().dispatchEvent(deathEvent, EventManager::DispatchMode::Immediate);

            // Also fire legacy CombatEvent for existing observers
            auto killEvent = std::make_shared<CombatEvent>(
                CombatEventType::NPCKilled, player, npc, attackDamage);
            EventManager::Instance().dispatchEvent(killEvent, EventManager::DispatchMode::Immediate);
        }
    }

    // Update target tracking
    if (closestHit) {
        m_targetedNPC = closestHit;
        m_targetDisplayTimer = TARGET_DISPLAY_DURATION;
    }
}

void CombatController::regenerateStamina(Player* player, float deltaTime)
{
    if (!player) {
        return;
    }

    float currentStamina = player->getStamina();
    float maxStamina = player->getMaxStamina();

    if (currentStamina < maxStamina) {
        float regenAmount = STAMINA_REGEN_RATE * deltaTime;
        player->restoreStamina(regenAmount);
    }
}

void CombatController::updateTargetTimer(float deltaTime)
{
    if (m_targetDisplayTimer > 0.0f) {
        m_targetDisplayTimer -= deltaTime;
        if (m_targetDisplayTimer <= 0.0f) {
            m_targetDisplayTimer = 0.0f;
            m_targetedNPC.reset();
            COMBAT_DEBUG("Target display timer expired");
        }
    }
}

std::shared_ptr<NPC> CombatController::getTargetedNPC() const
{
    // Safely lock weak_ptr and verify target is still valid (alive)
    auto target = m_targetedNPC.lock();
    if (!target || !target->isAlive()) {
        return nullptr;
    }
    return target;
}

bool CombatController::hasActiveTarget() const
{
    return m_targetDisplayTimer > 0.0f && getTargetedNPC() != nullptr;
}
