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
#include "managers/GameTimeManager.hpp"
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

  const Vector2D playerPos = player->getPosition();
  const float attackRange = player->getAttackRange();
  const float attackDamage = player->getAttackDamage();

  // Determine attack direction based on player facing
  float attackDirX = (player->getFlip() == SDL_FLIP_HORIZONTAL) ? -1.0f : 1.0f;

  // Query nearby entity handles from AIManager (EntityHandle-based API)
  std::vector<EntityHandle> nearbyHandles;
  AIManager::Instance().queryHandlesInRadius(playerPos, attackRange,
                                             nearbyHandles, true);

  // Get EntityDataManager for position lookups
  auto &edm = EntityDataManager::Instance();

  // Check all nearby entities for hits
  EntityHandle closestHandle{}; // Invalid handle by default
  float closestDist = attackRange + 1.0f;

  // Get player's EntityHandle for event-driven damage
  EntityHandle playerHandle = player->getHandle();

  for (const auto &handle : nearbyHandles) {
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

    // Phase 2 EDM Migration: Use CharacterData for health
    auto &charData = edm.getCharacterData(handle);
    float oldHealth = charData.health;

    // Fire DamageIntent event for any observers
    auto damageIntent = std::make_shared<DamageEvent>(
        EntityEventType::DamageIntent, playerHandle, handle, attackDamage,
        knockback);
    EventManager::Instance().dispatchEvent(
        damageIntent, EventManager::DispatchMode::Immediate);

    // Apply damage directly to CharacterData
    charData.health = std::max(0.0f, charData.health - attackDamage);

    // Record combat event in NPC's memory (they were attacked by player)
    float gameTime = GameTimeManager::Instance().getTotalGameTimeSeconds();
    edm.recordCombatEvent(idx, playerHandle, handle, attackDamage,
                          /*wasAttacked=*/true, gameTime);

    // Combat response: Non-hostile entities flee when attacked
    if (charData.faction != 1) { // Friendly (0) or Neutral (2)
      // Switch to flee behavior
      AIManager::Instance().assignBehavior(handle, "Flee");

      // Alert nearby guards (broadcast to guard behavior group)
      AIManager::Instance().broadcastMessage(
          std::format("alert_attacker:{}", playerHandle.getId()));
    }

    // Apply knockback via velocity
    hotData.transform.velocity = hotData.transform.velocity + knockback;

    // Get entity name for display (use kind + ID for now)
    std::string entityName = std::format("Enemy #{}", handle.getId());

    COMBAT_INFO(
        std::format("Hit entity {} for {:.1f} damage! HP: {:.1f} -> {:.1f}",
                    handle.getId(), attackDamage, oldHealth, charData.health));

    // Add to on-screen event log
    UIManager::Instance().addEventLogEntry(
        GAMEPLAY_EVENT_LOG,
        std::format("Hit {} for {:.0f} damage!", entityName, attackDamage));

    // Track closest hit for targeting (using handle for now)
    if (distance < closestDist) {
      closestDist = distance;
      closestHandle = handle;
    }

    // Fire CombatEvent for UI/sound observers (using handles)
    // Note: CombatEvent may need updating to use handles instead of EntityPtr
    // For now, dispatch DamageEvent which already uses handles

    // Check for kill
    if (charData.health <= 0.0f) {
      // Mark as dead in HotData
      hotData.flags &= ~EntityHotData::FLAG_ALIVE;

      COMBAT_INFO(std::format("Entity {} killed!", handle.getId()));

      // Add kill to on-screen event log
      UIManager::Instance().addEventLogEntry(
          GAMEPLAY_EVENT_LOG,
          std::format("Defeated {}!", entityName));

      // Fire DeathEvent for entity lifecycle observers
      auto deathEvent = std::make_shared<DeathEvent>(
          EntityEventType::DeathCompleted, handle, playerHandle);
      deathEvent->setDeathPosition(npcPos);
      EventManager::Instance().dispatchEvent(
          deathEvent, EventManager::DispatchMode::Immediate);
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
