/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/combat/CombatController.hpp"
#include "core/Logger.hpp"
#include "entities/Player.hpp"
#include "events/CombatEvent.hpp"
#include "events/EntityEvents.hpp"
#include "managers/AIManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/UIManager.hpp"
#include <format>

namespace {
    constexpr const char* GAMEPLAY_EVENT_LOG = "gameplay_event_log";
}

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

void CombatController::update(float deltaTime) {
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

bool CombatController::tryAttack() {
  auto player = mp_player.lock();
  if (!player) {
    return false;
  }

  // Check cooldown
  if (m_attackCooldown > 0.0f) {
    COMBAT_DEBUG(
        std::format("Attack on cooldown: {:.2f}s remaining", m_attackCooldown));
    return false;
  }

  // Check stamina
  if (!player->canAttack(ATTACK_STAMINA_COST)) {
    COMBAT_DEBUG(
        std::format("Not enough stamina to attack. Need {:.1f}, have {:.1f}",
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
      CombatEventType::PlayerAttacked, player.get(), nullptr,
      player->getAttackDamage());
  EventManager::Instance().dispatchEvent(attackEvent,
                                         EventManager::DispatchMode::Immediate);

  return true;
}

void CombatController::performAttack(Player *player) {
  if (!player) {
    return;
  }

  // Cache manager references at function scope
  auto& edm = EntityDataManager::Instance();
  auto& aiMgr = AIManager::Instance();
  auto& uiMgr = UIManager::Instance();

  const Vector2D playerPos = player->getPosition();
  const float attackRange = player->getAttackRange();
  const float attackDamage = player->getAttackDamage();

  // Determine attack direction based on player facing
  float attackDirX = (player->getFlip() == SDL_FLIP_HORIZONTAL) ? -1.0f : 1.0f;

  // Query nearby entity handles from AIManager (EntityHandle-based API)
  // Reuse buffer to avoid per-frame allocation
  m_nearbyHandlesBuffer.clear();  // Keeps capacity
  aiMgr.queryHandlesInRadius(playerPos, attackRange,
                             m_nearbyHandlesBuffer, true);

  // Check all nearby entities for hits
  EntityHandle closestHandle{}; // Invalid handle by default
  float closestDist = attackRange + 1.0f;

  // Get player's EntityHandle for event-driven damage
  EntityHandle playerHandle = player->getHandle();

  for (const auto &handle : m_nearbyHandlesBuffer) {
    if (!handle.isValid())
      continue;

    // Phase 2 EDM Migration: Use handle.getKind() instead of EntityPtr
    if (handle.getKind() != EntityKind::NPC) {
      continue;
    }

    size_t idx = edm.getIndex(handle);
    if (idx == SIZE_MAX)
      continue;

    // Get entity data from EDM (single source of truth)
    auto &hotData = edm.getHotDataByIndex(idx);

    // Use EDM's isAlive() instead of Entity method
    if (!hotData.isAlive()) {
      continue;
    }

    const Vector2D npcPos = hotData.transform.position;
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

    // Record pre-damage health for UI logging
    float oldHealth = edm.getCharacterData(handle).health;

    // Dispatch DamageEvent via Combat type — AIManager handler applies
    // damage, knockback, records combat events, notifies witnesses, handles death
    auto& eventMgr = EventManager::Instance();
    auto damageEvent = eventMgr.acquireDamageEvent();
    damageEvent->configure(playerHandle, handle, attackDamage, knockback);
    eventMgr.dispatchEvent(
        damageEvent, EventManager::DispatchMode::Immediate);

    COMBAT_INFO(
        std::format("Hit entity {} for {:.1f} damage! HP: {:.1f} -> {:.1f}",
                    handle.getId(), attackDamage, oldHealth,
                    damageEvent->getRemainingHealth()));

    uiMgr.addEventLogEntry(
        GAMEPLAY_EVENT_LOG,
        std::format("Hit Enemy #{} for {:.0f} damage!", handle.getId(), attackDamage));

    // Track closest hit for targeting
    if (distance < closestDist) {
      closestDist = distance;
      closestHandle = handle;
    }

    // Kill notification for UI
    if (damageEvent->wasLethal()) {
      COMBAT_INFO(std::format("Entity {} killed!", handle.getId()));

      uiMgr.addEventLogEntry(
          GAMEPLAY_EVENT_LOG,
          std::format("Defeated Enemy #{}!", handle.getId()));
    }
  }

  // Update target tracking (using handle)
  if (closestHandle.isValid()) {
    m_targetedHandle = closestHandle;
    m_targetDisplayTimer = TARGET_DISPLAY_DURATION;
  }
}

void CombatController::regenerateStamina(Player *player, float deltaTime) {
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

void CombatController::updateTargetTimer(float deltaTime) {
  if (m_targetDisplayTimer > 0.0f) {
    m_targetDisplayTimer -= deltaTime;
    if (m_targetDisplayTimer <= 0.0f) {
      m_targetDisplayTimer = 0.0f;
      m_targetedHandle = EntityHandle{}; // Clear handle
      COMBAT_DEBUG("Target display timer expired");
    }
  }
}

bool CombatController::hasActiveTarget() const {
  // Phase 2 EDM Migration: Use handle + EDM check
  if (m_targetDisplayTimer <= 0.0f || !m_targetedHandle.isValid()) {
    return false;
  }

  auto &edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(m_targetedHandle);
  if (idx == SIZE_MAX) {
    return false;
  }

  return edm.getHotDataByIndex(idx).isAlive();
}

float CombatController::getTargetHealth() const {
  if (!m_targetedHandle.isValid()) {
    return 0.0f;
  }

  auto &edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(m_targetedHandle);
  if (idx == SIZE_MAX) {
    return 0.0f;
  }

  return edm.getCharacterDataByIndex(idx).health;
}
