/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/social/SocialController.hpp"
#include "core/Logger.hpp"
#include "entities/Player.hpp"
#include "events/EntityEvents.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/GameTimeManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/UIManager.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace {
    constexpr const char* GAMEPLAY_EVENT_LOG = "gameplay_event_log";
}

void SocialController::subscribe() {
    if (checkAlreadySubscribed()) {
        return;
    }

    // SocialController doesn't subscribe to events currently
    // It's driven by player actions (tryBuy, trySell, tryGift)
    // Future: could subscribe to theft detection events

    setSubscribed(true);
    SOCIAL_INFO("SocialController subscribed");
}

// ============================================================================
// TRADING
// ============================================================================

TradeResult SocialController::tryBuy(EntityHandle npcHandle,
                                     HammerEngine::ResourceHandle itemHandle,
                                     int quantity) {
    auto player = mp_player.lock();
    if (!player) {
        return TradeResult::InvalidNPC;
    }

    // Validate NPC
    if (!npcHandle.isValid()) {
        SOCIAL_DEBUG("tryBuy: Invalid NPC handle");
        return TradeResult::InvalidNPC;
    }

    auto& edm = EntityDataManager::Instance();
    size_t npcIdx = edm.getIndex(npcHandle);
    if (npcIdx == SIZE_MAX) {
        SOCIAL_DEBUG("tryBuy: NPC not found in EDM");
        return TradeResult::InvalidNPC;
    }

    // Check if NPC is a merchant
    if (!isMerchant(npcHandle)) {
        SOCIAL_DEBUG("tryBuy: NPC is not a merchant");
        return TradeResult::InvalidNPC;
    }

    // Check relationship - NPC may refuse
    if (willRefuseTrade(npcHandle)) {
        SOCIAL_INFO("tryBuy: NPC refused trade due to poor relationship");
        return TradeResult::NPCRefused;
    }

    // Validate item
    if (!itemHandle.isValid()) {
        return TradeResult::InvalidItem;
    }

    // Check NPC has the item
    uint32_t npcInvIdx = getNPCInventoryIndex(npcHandle);
    if (!edm.hasInInventory(npcInvIdx, itemHandle, quantity)) {
        SOCIAL_DEBUG(std::format("tryBuy: NPC doesn't have {} of item", quantity));
        return TradeResult::InsufficientStock;
    }

    // Calculate price
    float totalPrice = calculateBuyPrice(npcHandle, itemHandle, quantity);
    int priceInt = static_cast<int>(std::ceil(totalPrice));

    // Check player has enough gold
    if (!player->hasGold(priceInt)) {
        SOCIAL_DEBUG(std::format("tryBuy: Player doesn't have {} gold", priceInt));
        return TradeResult::InsufficientFunds;
    }

    // Execute trade
    // 1. Remove item from NPC inventory
    if (!edm.removeFromInventory(npcInvIdx, itemHandle, quantity)) {
        SOCIAL_ERROR("tryBuy: Failed to remove item from NPC inventory");
        return TradeResult::InsufficientStock;
    }

    // 2. Add item to player inventory
    if (!player->addToInventory(itemHandle, quantity)) {
        // Rollback - return item to NPC
        edm.addToInventory(npcInvIdx, itemHandle, quantity);
        SOCIAL_DEBUG("tryBuy: Player inventory full");
        return TradeResult::InventoryFull;
    }

    // 3. Transfer gold from player to NPC
    player->removeGold(priceInt);
    edm.addToInventory(npcInvIdx, ResourceTemplateManager::Instance().getHandleByName("gold"), priceInt);

    // Record the trade in NPC's memory
    recordTrade(npcHandle, totalPrice, true);

    SOCIAL_INFO(std::format("Trade complete: Player bought {} items (value: {:.1f})",
                            quantity, totalPrice));

    return TradeResult::Success;
}

TradeResult SocialController::trySell(EntityHandle npcHandle,
                                      HammerEngine::ResourceHandle itemHandle,
                                      int quantity) {
    auto player = mp_player.lock();
    if (!player) {
        return TradeResult::InvalidNPC;
    }

    // Validate NPC
    if (!npcHandle.isValid()) {
        SOCIAL_DEBUG("trySell: Invalid NPC handle");
        return TradeResult::InvalidNPC;
    }

    auto& edm = EntityDataManager::Instance();
    size_t npcIdx = edm.getIndex(npcHandle);
    if (npcIdx == SIZE_MAX) {
        SOCIAL_DEBUG("trySell: NPC not found in EDM");
        return TradeResult::InvalidNPC;
    }

    // Check if NPC is a merchant
    if (!isMerchant(npcHandle)) {
        SOCIAL_DEBUG("trySell: NPC is not a merchant");
        return TradeResult::InvalidNPC;
    }

    // Check relationship - NPC may refuse
    if (willRefuseTrade(npcHandle)) {
        SOCIAL_INFO("trySell: NPC refused trade due to poor relationship");
        return TradeResult::NPCRefused;
    }

    // Validate item
    if (!itemHandle.isValid()) {
        return TradeResult::InvalidItem;
    }

    // Check player has the item
    if (!player->hasInInventory(itemHandle, quantity)) {
        SOCIAL_DEBUG(std::format("trySell: Player doesn't have {} of item", quantity));
        return TradeResult::InsufficientStock;
    }

    // Calculate price
    float totalPrice = calculateSellPrice(npcHandle, itemHandle, quantity);
    int priceInt = static_cast<int>(std::floor(totalPrice));

    // Check NPC has enough gold to pay
    uint32_t npcInvIdx = getNPCInventoryIndex(npcHandle);
    auto goldHandle = ResourceTemplateManager::Instance().getHandleByName("gold");
    if (!edm.hasInInventory(npcInvIdx, goldHandle, priceInt)) {
        SOCIAL_DEBUG(std::format("trySell: NPC doesn't have {} gold to pay", priceInt));
        return TradeResult::InsufficientFunds;
    }

    // Execute trade
    // 1. Remove item from player inventory
    if (!player->removeFromInventory(itemHandle, quantity)) {
        SOCIAL_ERROR("trySell: Failed to remove item from player inventory");
        return TradeResult::InsufficientStock;
    }

    // 2. Add item to NPC inventory
    if (!edm.addToInventory(npcInvIdx, itemHandle, quantity)) {
        // Rollback - return item to player
        player->addToInventory(itemHandle, quantity);
        SOCIAL_DEBUG("trySell: NPC inventory full");
        return TradeResult::InventoryFull;
    }

    // 3. Transfer gold from NPC to player
    edm.removeFromInventory(npcInvIdx, goldHandle, priceInt);
    player->addGold(priceInt);

    // Record the trade in NPC's memory
    recordTrade(npcHandle, totalPrice, true);

    SOCIAL_INFO(std::format("Trade complete: Player sold {} items (value: {:.1f})",
                            quantity, totalPrice));

    return TradeResult::Success;
}

float SocialController::calculateBuyPrice(EntityHandle npcHandle,
                                          HammerEngine::ResourceHandle itemHandle,
                                          int quantity) const {
    float baseValue = getItemBaseValue(itemHandle);
    float modifier = getPriceModifier(npcHandle);

    return baseValue * modifier * BUY_PRICE_MULTIPLIER * quantity;
}

float SocialController::calculateSellPrice(EntityHandle npcHandle,
                                           HammerEngine::ResourceHandle itemHandle,
                                           int quantity) const {
    float baseValue = getItemBaseValue(itemHandle);
    float modifier = getPriceModifier(npcHandle);

    // Better relationship = better sell price (inverse of buy modifier)
    float sellModifier = 2.0f - modifier;  // If buy is 0.7x, sell becomes 1.3x

    return baseValue * sellModifier * SELL_PRICE_MULTIPLIER * quantity;
}

// ============================================================================
// GIFTS & INTERACTIONS
// ============================================================================

bool SocialController::tryGift(EntityHandle npcHandle,
                               HammerEngine::ResourceHandle itemHandle,
                               int quantity) {
    auto player = mp_player.lock();
    if (!player) {
        return false;
    }

    // Validate NPC
    if (!npcHandle.isValid()) {
        SOCIAL_DEBUG("tryGift: Invalid NPC handle");
        return false;
    }

    const auto& edm = EntityDataManager::Instance();
    size_t npcIdx = edm.getIndex(npcHandle);
    if (npcIdx == SIZE_MAX) {
        SOCIAL_DEBUG("tryGift: NPC not found in EDM");
        return false;
    }

    // Validate item
    if (!itemHandle.isValid()) {
        return false;
    }

    // Check player has the item
    if (!player->hasInInventory(itemHandle, quantity)) {
        SOCIAL_DEBUG(std::format("tryGift: Player doesn't have {} of item", quantity));
        return false;
    }

    // Remove item from player inventory
    if (!player->removeFromInventory(itemHandle, quantity)) {
        SOCIAL_ERROR("tryGift: Failed to remove item from player inventory");
        return false;
    }

    // Calculate gift value
    float giftValue = getItemBaseValue(itemHandle) * quantity;

    // Record the gift in NPC's memory
    recordGift(npcHandle, giftValue);

    SOCIAL_INFO(std::format("Gift given: {} items worth {:.1f} gold",
                            quantity, giftValue));

    return true;
}

void SocialController::recordInteraction(EntityHandle npcHandle,
                                         InteractionType type,
                                         float value) {
    if (!npcHandle.isValid()) {
        return;
    }

    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(npcHandle);
    if (idx == SIZE_MAX) {
        return;
    }

    // Get player handle for memory subject
    auto player = mp_player.lock();
    EntityHandle playerHandle = player ? player->getHandle() : EntityHandle{};

    // Create memory entry
    MemoryEntry entry;
    entry.subject = playerHandle;
    entry.location = edm.getHotDataByIndex(idx).transform.position;
    entry.timestamp = GameTimeManager::Instance().getTotalGameTimeSeconds();
    entry.value = value;
    entry.type = MemoryType::Interaction;
    entry.flags = MemoryEntry::FLAG_VALID;

    // Set importance based on interaction type and value
    float importance = std::abs(value) * 50.0f;
    switch (type) {
        case InteractionType::Gift:
            importance += 50.0f;
            break;
        case InteractionType::Help:
            importance += 75.0f;
            break;
        case InteractionType::Theft:
            importance = 200.0f;  // Always very important
            break;
        case InteractionType::Trade:
            importance += 25.0f;
            break;
        default:
            importance += 10.0f;
            break;
    }
    entry.importance = static_cast<uint8_t>(std::min(255.0f, importance));

    // Add to NPC's memory
    edm.addMemory(idx, entry);

    // Update emotions
    updateEmotions(npcHandle, type, value);
}

void SocialController::reportTheft(EntityHandle thief,
                                   EntityHandle victim,
                                   HammerEngine::ResourceHandle stolenItem,
                                   int quantity) {
    if (!victim.isValid()) {
        SOCIAL_DEBUG("reportTheft: Invalid victim handle");
        return;
    }

    auto& edm = EntityDataManager::Instance();
    size_t victimIdx = edm.getIndex(victim);
    if (victimIdx == SIZE_MAX) {
        SOCIAL_DEBUG("reportTheft: Victim not found in EDM");
        return;
    }

    // 1. Record the theft in victim's memory (severe relationship damage)
    recordInteraction(victim, InteractionType::Theft, THEFT_RELATIONSHIP_LOSS);

    // 2. Get the location of the theft
    Vector2D theftLocation = edm.getHotDataByIndex(victimIdx).transform.position;

    // 3. Fire TheftEvent to alert nearby guards
    auto theftEvent = std::make_shared<TheftEvent>(
        thief, victim, stolenItem, quantity, theftLocation);
    EventManager::Instance().dispatchEvent(theftEvent, EventManager::DispatchMode::Immediate);

    // Get item name for logging
    const auto& rtm = ResourceTemplateManager::Instance();
    auto resTemplate = rtm.getResourceTemplate(stolenItem);
    std::string itemName = resTemplate ? resTemplate->getName() : "unknown item";

    SOCIAL_INFO(std::format("Theft reported: {} x{} stolen at ({:.0f}, {:.0f})",
                            itemName, quantity, theftLocation.getX(), theftLocation.getY()));

    // 4. Alert nearby guards
    alertNearbyGuards(theftLocation, thief);
}

void SocialController::alertNearbyGuards(const Vector2D& location, EntityHandle criminal) {
    auto& edm = EntityDataManager::Instance();

    // Iterate active entities looking for guards within alert range
    auto activeIndices = edm.getActiveIndices();
    int guardsAlerted = 0;

    for (size_t idx : activeIndices) {
        // Check if this entity has Guard behavior
        const auto& behaviorData = edm.getBehaviorData(idx);
        if (behaviorData.behaviorType != BehaviorType::Guard) {
            continue;
        }

        // Check distance from theft location
        const auto& transform = edm.getHotDataByIndex(idx).transform;
        float distance = (transform.position - location).length();

        if (distance <= GUARD_ALERT_RANGE) {
            // Alert this guard by setting their alert state
            auto& guardData = edm.getBehaviorData(idx);
            auto& guard = guardData.state.guard;

            // Set to HOSTILE alert level (level 3)
            guard.currentAlertLevel = 3;
            guard.alertTimer = 0.0f;
            guard.lastKnownThreatPosition = location;
            guard.alertRaised = true;
            guard.hasActiveThreat = true;

            // Note: Guard will investigate lastKnownThreatPosition
            // and transition to Attack behavior if they find the target
            (void)criminal;  // Target tracking happens via normal guard threat detection

            ++guardsAlerted;
            SOCIAL_DEBUG(std::format("Guard at ({:.0f}, {:.0f}) alerted to theft",
                                     transform.position.getX(), transform.position.getY()));
        }
    }

    if (guardsAlerted > 0) {
        SOCIAL_INFO(std::format("Alerted {} guards to theft at ({:.0f}, {:.0f})",
                                guardsAlerted, location.getX(), location.getY()));

        // Add to on-screen event log
        UIManager::Instance().addEventLogEntry(
            GAMEPLAY_EVENT_LOG,
            std::format("Guards alerted! {} guards responding to crime.", guardsAlerted));
    }
}

// ============================================================================
// RELATIONSHIP
// ============================================================================

float SocialController::getRelationshipLevel(EntityHandle npcHandle) const {
    if (!npcHandle.isValid()) {
        return RELATIONSHIP_NEUTRAL;
    }

    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(npcHandle);
    if (idx == SIZE_MAX) {
        return RELATIONSHIP_NEUTRAL;
    }

    // Check if NPC has memory data
    if (!edm.hasMemoryData(idx)) {
        return RELATIONSHIP_NEUTRAL;
    }

    const auto& memoryData = edm.getMemoryData(idx);
    if (!memoryData.isValid()) {
        return RELATIONSHIP_NEUTRAL;
    }

    // Calculate relationship from emotional state
    // Low suspicion + low fear + low aggression = good relationship
    const auto& emotions = memoryData.emotions;

    float relationship = 0.0f;

    // Negative emotions reduce relationship
    relationship -= emotions.suspicion * 0.4f;
    relationship -= emotions.fear * 0.3f;
    relationship -= emotions.aggression * 0.5f;

    // Check interaction memories for additional context
    auto player = mp_player.lock();
    if (player) {
        std::vector<const MemoryEntry*> memories;
        edm.findMemoriesOfEntity(idx, player->getHandle(), memories);

        // Sum up interaction values (positive = good, negative = bad)
        for (const auto* mem : memories) {
            if (mem && mem->type == MemoryType::Interaction) {
                relationship += mem->value * 0.1f;  // Scale down memory influence
            }
        }
    }

    return std::clamp(relationship, -1.0f, 1.0f);
}

float SocialController::getPriceModifier(EntityHandle npcHandle) const {
    float relationship = getRelationshipLevel(npcHandle);

    // Map relationship [-1, 1] to price modifier [1.3, 0.7]
    // -1.0 (hostile) -> 1.3x prices (30% more expensive)
    //  0.0 (neutral) -> 1.0x prices (normal)
    // +1.0 (trusted) -> 0.7x prices (30% cheaper)

    return 1.0f - (relationship * 0.3f);
}

bool SocialController::willRefuseTrade(EntityHandle npcHandle) const {
    float relationship = getRelationshipLevel(npcHandle);
    return relationship < RELATIONSHIP_HOSTILE;
}

std::string SocialController::getRelationshipDescription(EntityHandle npcHandle) const {
    float relationship = getRelationshipLevel(npcHandle);

    if (relationship >= RELATIONSHIP_TRUSTED) {
        return "Trusted Friend";
    } else if (relationship >= RELATIONSHIP_FRIENDLY) {
        return "Friendly";
    } else if (relationship >= RELATIONSHIP_NEUTRAL - 0.1f) {
        return "Neutral";
    } else if (relationship >= RELATIONSHIP_UNFRIENDLY) {
        return "Unfriendly";
    } else if (relationship >= RELATIONSHIP_HOSTILE) {
        return "Hostile";
    } else {
        return "Despised";
    }
}

// ============================================================================
// NPC INVENTORY HELPERS
// ============================================================================

bool SocialController::isMerchant(EntityHandle npcHandle) const {
    return EntityDataManager::Instance().isNPCMerchant(npcHandle);
}

uint32_t SocialController::getNPCInventoryIndex(EntityHandle npcHandle) const {
    return EntityDataManager::Instance().getNPCInventoryIndex(npcHandle);
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void SocialController::recordTrade(EntityHandle npcHandle, float tradeValue, bool wasGoodDeal) {
    float value = wasGoodDeal ? (tradeValue * 0.01f) : -(tradeValue * 0.005f);
    recordInteraction(npcHandle, InteractionType::Trade, value);
}

void SocialController::recordGift(EntityHandle npcHandle, float giftValue) {
    // Gift value scales the relationship gain
    float value = GIFT_RELATIONSHIP_BASE + (giftValue * GIFT_VALUE_SCALE);
    recordInteraction(npcHandle, InteractionType::Gift, value);
}

void SocialController::updateEmotions(EntityHandle npcHandle,
                                      InteractionType type,
                                      float value) {
    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(npcHandle);
    if (idx == SIZE_MAX) {
        return;
    }

    float aggression = 0.0f;
    float fear = 0.0f;
    float curiosity = 0.0f;
    float suspicion = 0.0f;

    switch (type) {
        case InteractionType::Trade:
            // Successful trades reduce suspicion
            suspicion = -0.05f * (value > 0 ? 1.0f : -0.5f);
            break;

        case InteractionType::Gift:
            // Gifts significantly reduce negative emotions
            suspicion = -0.15f;
            aggression = -0.1f;
            fear = -0.05f;
            break;

        case InteractionType::Greeting:
            // Small positive effect
            suspicion = -0.02f;
            break;

        case InteractionType::Help:
            // Helping has strong positive effect
            suspicion = -0.2f;
            aggression = -0.15f;
            fear = -0.1f;
            break;

        case InteractionType::Theft:
            // Theft has strong negative effect
            suspicion = 0.4f;
            aggression = 0.3f;
            fear = 0.1f;
            break;

        case InteractionType::Insult:
            // Insults cause hostility
            aggression = 0.2f;
            suspicion = 0.15f;
            break;
    }

    edm.modifyEmotions(idx, aggression, fear, curiosity, suspicion);
}

float SocialController::getItemBaseValue(HammerEngine::ResourceHandle itemHandle) const {
    if (!itemHandle.isValid()) {
        return 0.0f;
    }

    return ResourceTemplateManager::Instance().getValue(itemHandle);
}
