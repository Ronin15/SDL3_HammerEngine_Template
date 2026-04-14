/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/ui/GameplayHUDController.hpp"
#include "events/EntityEvents.hpp"
#include "managers/EntityDataManager.hpp"

void GameplayHUDController::subscribe()
{
    if (checkAlreadySubscribed())
    {
        return;
    }

    auto token = EventManager::Instance().registerHandlerWithToken(
        EventTypeId::Combat,
        [this](const EventData& data) { onCombatEvent(data); });
    addHandlerToken(token);
    setSubscribed(true);
}

void GameplayHUDController::update(float deltaTime)
{
    if (m_targetDisplayTimer > 0.0f)
    {
        m_targetDisplayTimer -= deltaTime;
        if (m_targetDisplayTimer <= 0.0f)
        {
            clearTarget();
            return;
        }
    }

    if (!m_targetedHandle.isValid())
    {
        return;
    }

    auto& edm = EntityDataManager::Instance();
    const size_t targetIdx = edm.getIndex(m_targetedHandle);
    if (targetIdx == SIZE_MAX || !edm.getHotDataByIndex(targetIdx).isAlive())
    {
        clearTarget();
        return;
    }

    const auto& charData = edm.getCharacterDataByIndex(targetIdx);
    m_cachedTargetMaxHealth = charData.maxHealth;
    m_cachedTargetHealth = (charData.maxHealth > 0.0f)
        ? (charData.health / charData.maxHealth) * 100.0f
        : 0.0f;
}

bool GameplayHUDController::hasActiveTarget() const
{
    if (m_targetDisplayTimer <= 0.0f || !m_targetedHandle.isValid())
    {
        return false;
    }

    auto& edm = EntityDataManager::Instance();
    const size_t targetIdx = edm.getIndex(m_targetedHandle);
    if (targetIdx == SIZE_MAX)
    {
        return false;
    }

    return edm.getHotDataByIndex(targetIdx).isAlive();
}

float GameplayHUDController::getTargetHealth() const
{
    if (!hasActiveTarget())
    {
        return 0.0f;
    }

    return m_cachedTargetHealth;
}

void GameplayHUDController::onCombatEvent(const EventData& data)
{
    if (!data.isActive() || !data.event || !m_playerHandle.isValid())
    {
        return;
    }

    auto damageEvent = std::dynamic_pointer_cast<DamageEvent>(data.event);
    if (!damageEvent || damageEvent->getSource() != m_playerHandle)
    {
        return;
    }

    const EntityHandle targetHandle = damageEvent->getTarget();
    if (!targetHandle.isValid() || !targetHandle.isNPC())
    {
        return;
    }

    if (damageEvent->wasLethal())
    {
        clearTarget();
        return;
    }

    auto& edm = EntityDataManager::Instance();
    const size_t targetIdx = edm.getIndex(targetHandle);
    if (targetIdx == SIZE_MAX || !edm.getHotDataByIndex(targetIdx).isAlive())
    {
        clearTarget();
        return;
    }

    m_targetedHandle = targetHandle;
    m_targetDisplayTimer = TARGET_DISPLAY_DURATION;
    const float maxHealth = edm.getCharacterDataByIndex(targetIdx).maxHealth;
    m_cachedTargetMaxHealth = maxHealth;
    m_cachedTargetHealth = (maxHealth > 0.0f)
        ? (damageEvent->getRemainingHealth() / maxHealth) * 100.0f
        : 0.0f;
    if (targetHandle != m_lastLabeledHandle)
    {
        m_targetLabel = edm.getCreatureDisplayName(targetHandle);
        m_lastLabeledHandle = targetHandle;
    }
}

void GameplayHUDController::clearTarget()
{
    m_targetedHandle = EntityHandle{};
    m_lastLabeledHandle = EntityHandle{};
    m_targetDisplayTimer = 0.0f;
    m_cachedTargetHealth = 0.0f;
    if (m_targetLabel != "Target")
    {
        m_targetLabel = "Target";
    }
}
