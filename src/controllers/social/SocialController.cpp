/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/social/SocialController.hpp"
#include "ai/BehaviorExecutors.hpp"
#include "core/Logger.hpp"
#include "managers/AIManager.hpp"
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

    // Driven by player actions (tryBuy, trySell, tryGift, recordInteraction)
    // Future: subscribe to proximity/conversation events for social interactions

    setSubscribed(true);
    SOCIAL_INFO("SocialController subscribed");
}

void SocialController::update([[maybe_unused]] float deltaTime) {
    if (!m_isTrading) {
        return;
    }

    updatePriceDisplay();
}

// ============================================================================
// TRADING — Session Management
// ============================================================================

bool SocialController::openTrade(EntityHandle npcHandle) {
    if (m_isTrading) {
        SOCIAL_DEBUG("Already in trade session");
        return false;
    }

    auto player = mp_player.lock();
    if (!player) {
        SOCIAL_ERROR("Player reference lost");
        return false;
    }

    if (!isMerchant(npcHandle)) {
        SOCIAL_DEBUG("NPC is not a merchant");
        return false;
    }

    if (willRefuseTrade(npcHandle)) {
        SOCIAL_INFO("Merchant refused to trade (relationship too low)");
        return false;
    }

    m_merchantHandle = npcHandle;
    m_isTrading = true;
    m_selectedMerchantIndex = -1;
    m_selectedPlayerIndex = -1;
    m_quantity = 1;
    m_priceDisplayDirty = true;

    refreshMerchantItems();
    refreshPlayerItems();
    createTradeUI();

    SOCIAL_INFO("Trade session opened");
    return true;
}

void SocialController::closeTrade() {
    if (!m_isTrading) {
        SOCIAL_DEBUG("closeTrade called but m_isTrading is false");
        return;
    }

    SOCIAL_INFO("closeTrade executing - destroying UI");
    destroyTradeUI();

    m_isTrading = false;
    m_merchantHandle = EntityHandle{};
    m_merchantItems.clear();
    m_playerItems.clear();
    m_selectedMerchantIndex = -1;
    m_selectedPlayerIndex = -1;
    m_quantity = 1;
    m_priceDisplayDirty = true;

    SOCIAL_INFO("Trade session closed");
}

// ============================================================================
// TRADING — Item Selection & Transactions
// ============================================================================

void SocialController::selectMerchantItem(size_t index) {
    if (index < m_merchantItems.size()) {
        m_selectedMerchantIndex = static_cast<int>(index);
        m_selectedPlayerIndex = -1;
        m_quantity = 1;
        m_priceDisplayDirty = true;
        SOCIAL_DEBUG(std::format("Selected merchant item: {}", m_merchantItems[index].name));
    }
}

void SocialController::selectPlayerItem(size_t index) {
    if (index < m_playerItems.size()) {
        m_selectedPlayerIndex = static_cast<int>(index);
        m_selectedMerchantIndex = -1;
        m_quantity = 1;
        m_priceDisplayDirty = true;
        SOCIAL_DEBUG(std::format("Selected player item: {}", m_playerItems[index].name));
    }
}

void SocialController::setQuantity(int qty) {
    m_quantity = std::max(1, qty);

    if (m_selectedMerchantIndex >= 0 &&
        static_cast<size_t>(m_selectedMerchantIndex) < m_merchantItems.size()) {
        m_quantity = std::min(m_quantity, m_merchantItems[m_selectedMerchantIndex].quantity);
    } else if (m_selectedPlayerIndex >= 0 &&
               static_cast<size_t>(m_selectedPlayerIndex) < m_playerItems.size()) {
        m_quantity = std::min(m_quantity, m_playerItems[m_selectedPlayerIndex].quantity);
    }

    m_priceDisplayDirty = true;
}

TradeResult SocialController::executeBuy() {
    if (!m_isTrading || m_selectedMerchantIndex < 0) {
        return TradeResult::InvalidItem;
    }

    if (static_cast<size_t>(m_selectedMerchantIndex) >= m_merchantItems.size()) {
        return TradeResult::InvalidItem;
    }

    const auto& item = m_merchantItems[m_selectedMerchantIndex];
    TradeResult result = tryBuy(m_merchantHandle, item.handle, m_quantity);

    if (result == TradeResult::Success) {
        float price = calculateBuyPrice(m_merchantHandle, item.handle, m_quantity);
        int savedQty = m_quantity;

        refreshMerchantItems();
        refreshPlayerItems();
        m_selectedMerchantIndex = -1;
        m_quantity = 1;
        m_priceDisplayDirty = true;
        SOCIAL_INFO(std::format("Bought {} x{}", item.name, savedQty));

        UIManager::Instance().addEventLogEntry(
            "gameplay_event_log",
            std::format("Bought {} x{} for {:.0f} gold", item.name, savedQty, price));
    }

    return result;
}

TradeResult SocialController::executeSell() {
    if (!m_isTrading || m_selectedPlayerIndex < 0) {
        return TradeResult::InvalidItem;
    }

    if (static_cast<size_t>(m_selectedPlayerIndex) >= m_playerItems.size()) {
        return TradeResult::InvalidItem;
    }

    const auto& item = m_playerItems[m_selectedPlayerIndex];
    TradeResult result = trySell(m_merchantHandle, item.handle, m_quantity);

    if (result == TradeResult::Success) {
        float price = calculateSellPrice(m_merchantHandle, item.handle, m_quantity);
        int savedQty = m_quantity;

        refreshMerchantItems();
        refreshPlayerItems();
        m_selectedPlayerIndex = -1;
        m_quantity = 1;
        m_priceDisplayDirty = true;
        SOCIAL_INFO(std::format("Sold {} x{}", item.name, savedQty));

        UIManager::Instance().addEventLogEntry(
            "gameplay_event_log",
            std::format("Sold {} x{} for {:.0f} gold", item.name, savedQty, price));
    }

    return result;
}

// ============================================================================
// TRADING — Accessors (current session)
// ============================================================================

float SocialController::getCurrentBuyPrice() const {
    if (m_selectedMerchantIndex < 0 ||
        static_cast<size_t>(m_selectedMerchantIndex) >= m_merchantItems.size()) {
        return 0.0f;
    }

    const auto& item = m_merchantItems[m_selectedMerchantIndex];
    return calculateBuyPrice(m_merchantHandle, item.handle, m_quantity);
}

float SocialController::getCurrentSellPrice() const {
    if (m_selectedPlayerIndex < 0 ||
        static_cast<size_t>(m_selectedPlayerIndex) >= m_playerItems.size()) {
        return 0.0f;
    }

    const auto& item = m_playerItems[m_selectedPlayerIndex];
    return calculateSellPrice(m_merchantHandle, item.handle, m_quantity);
}

std::string SocialController::getCurrentTradeRelationshipDescription() const {
    if (!m_isTrading) {
        return "N/A";
    }
    return getRelationshipDescription(m_merchantHandle);
}

float SocialController::getCurrentTradePriceModifier() const {
    if (!m_isTrading) {
        return 1.0f;
    }
    return getPriceModifier(m_merchantHandle);
}

// ============================================================================
// TRADING — Backend (buy/sell/price calculation)
// ============================================================================

TradeResult SocialController::tryBuy(EntityHandle npcHandle,
                                     VoidLight::ResourceHandle itemHandle,
                                     int quantity) {
    auto player = mp_player.lock();
    if (!player) {
        return TradeResult::InvalidNPC;
    }

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

    if (!isMerchant(npcHandle)) {
        SOCIAL_DEBUG("tryBuy: NPC is not a merchant");
        return TradeResult::InvalidNPC;
    }

    if (willRefuseTrade(npcHandle)) {
        SOCIAL_INFO("tryBuy: NPC refused trade due to poor relationship");
        return TradeResult::NPCRefused;
    }

    if (!itemHandle.isValid()) {
        return TradeResult::InvalidItem;
    }

    uint32_t npcInvIdx = getNPCInventoryIndex(npcHandle);
    if (!edm.hasInInventory(npcInvIdx, itemHandle, quantity)) {
        SOCIAL_DEBUG(std::format("tryBuy: NPC doesn't have {} of item", quantity));
        return TradeResult::InsufficientStock;
    }

    float totalPrice = calculateBuyPrice(npcHandle, itemHandle, quantity);
    int priceInt = static_cast<int>(std::ceil(totalPrice));

    if (!player->hasGold(priceInt)) {
        SOCIAL_DEBUG(std::format("tryBuy: Player doesn't have {} gold", priceInt));
        return TradeResult::InsufficientFunds;
    }

    if (!edm.removeFromInventory(npcInvIdx, itemHandle, quantity)) {
        SOCIAL_ERROR("tryBuy: Failed to remove item from NPC inventory");
        return TradeResult::InsufficientStock;
    }

    if (!player->addToInventory(itemHandle, quantity)) {
        edm.addToInventory(npcInvIdx, itemHandle, quantity);
        SOCIAL_DEBUG("tryBuy: Player inventory full");
        return TradeResult::InventoryFull;
    }

    player->removeGold(priceInt);
    edm.addToInventory(npcInvIdx, ResourceTemplateManager::Instance().getHandleByName("gold"), priceInt);

    recordTrade(npcHandle, totalPrice, true);

    SOCIAL_INFO(std::format("Trade complete: Player bought {} items (value: {:.1f})",
                            quantity, totalPrice));

    return TradeResult::Success;
}

TradeResult SocialController::trySell(EntityHandle npcHandle,
                                      VoidLight::ResourceHandle itemHandle,
                                      int quantity) {
    auto player = mp_player.lock();
    if (!player) {
        return TradeResult::InvalidNPC;
    }

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

    if (!isMerchant(npcHandle)) {
        SOCIAL_DEBUG("trySell: NPC is not a merchant");
        return TradeResult::InvalidNPC;
    }

    if (willRefuseTrade(npcHandle)) {
        SOCIAL_INFO("trySell: NPC refused trade due to poor relationship");
        return TradeResult::NPCRefused;
    }

    if (!itemHandle.isValid()) {
        return TradeResult::InvalidItem;
    }

    if (!player->hasInInventory(itemHandle, quantity)) {
        SOCIAL_DEBUG(std::format("trySell: Player doesn't have {} of item", quantity));
        return TradeResult::InsufficientStock;
    }

    float totalPrice = calculateSellPrice(npcHandle, itemHandle, quantity);
    int priceInt = static_cast<int>(std::floor(totalPrice));

    uint32_t npcInvIdx = getNPCInventoryIndex(npcHandle);
    auto goldHandle = ResourceTemplateManager::Instance().getHandleByName("gold");
    if (!edm.hasInInventory(npcInvIdx, goldHandle, priceInt)) {
        SOCIAL_DEBUG(std::format("trySell: NPC doesn't have {} gold to pay", priceInt));
        return TradeResult::InsufficientFunds;
    }

    if (!player->removeFromInventory(itemHandle, quantity)) {
        SOCIAL_ERROR("trySell: Failed to remove item from player inventory");
        return TradeResult::InsufficientStock;
    }

    if (!edm.addToInventory(npcInvIdx, itemHandle, quantity)) {
        player->addToInventory(itemHandle, quantity);
        SOCIAL_DEBUG("trySell: NPC inventory full");
        return TradeResult::InventoryFull;
    }

    edm.removeFromInventory(npcInvIdx, goldHandle, priceInt);
    player->addGold(priceInt);

    recordTrade(npcHandle, totalPrice, true);

    SOCIAL_INFO(std::format("Trade complete: Player sold {} items (value: {:.1f})",
                            quantity, totalPrice));

    return TradeResult::Success;
}

float SocialController::calculateBuyPrice(EntityHandle npcHandle,
                                          VoidLight::ResourceHandle itemHandle,
                                          int quantity) const {
    float baseValue = getItemBaseValue(itemHandle);
    float modifier = getPriceModifier(npcHandle);

    return baseValue * modifier * BUY_PRICE_MULTIPLIER * quantity;
}

float SocialController::calculateSellPrice(EntityHandle npcHandle,
                                           VoidLight::ResourceHandle itemHandle,
                                           int quantity) const {
    float baseValue = getItemBaseValue(itemHandle);
    float modifier = getPriceModifier(npcHandle);

    // Better relationship = better sell price (inverse of buy modifier)
    float sellModifier = 2.0f - modifier;

    return baseValue * sellModifier * SELL_PRICE_MULTIPLIER * quantity;
}

// ============================================================================
// SOCIAL — Interactions & Memory
// ============================================================================

bool SocialController::tryGift(EntityHandle npcHandle,
                               VoidLight::ResourceHandle itemHandle,
                               int quantity) {
    auto player = mp_player.lock();
    if (!player) {
        return false;
    }

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

    if (!itemHandle.isValid()) {
        return false;
    }

    if (!player->hasInInventory(itemHandle, quantity)) {
        SOCIAL_DEBUG(std::format("tryGift: Player doesn't have {} of item", quantity));
        return false;
    }

    if (!player->removeFromInventory(itemHandle, quantity)) {
        SOCIAL_ERROR("tryGift: Failed to remove item from player inventory");
        return false;
    }

    float giftValue = getItemBaseValue(itemHandle) * quantity;
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

    auto player = mp_player.lock();
    EntityHandle playerHandle = player ? player->getHandle() : EntityHandle{};

    MemoryEntry entry;
    entry.subject = playerHandle;
    entry.location = edm.getHotDataByIndex(idx).transform.position;
    entry.timestamp = GameTimeManager::Instance().getTotalGameTimeSeconds();
    entry.value = value;
    entry.type = MemoryType::Interaction;
    entry.flags = MemoryEntry::FLAG_VALID;

    float importance = std::abs(value) * 50.0f;
    switch (type) {
        case InteractionType::Gift:
            importance += 50.0f;
            break;
        case InteractionType::Help:
            importance += 75.0f;
            break;
        case InteractionType::Theft:
            importance = 200.0f;
            break;
        case InteractionType::Trade:
            importance += 25.0f;
            break;
        default:
            importance += 10.0f;
            break;
    }
    entry.importance = static_cast<uint8_t>(std::min(255.0f, importance));

    edm.addMemory(idx, entry);
    updateEmotions(npcHandle, type, value);
}

void SocialController::reportTheft(EntityHandle thief,
                                   EntityHandle victim,
                                   VoidLight::ResourceHandle stolenItem,
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

    recordInteraction(victim, InteractionType::Theft, THEFT_RELATIONSHIP_LOSS);

    Vector2D theftLocation = edm.getHotDataByIndex(victimIdx).transform.position;

    auto theftEvent = std::make_shared<TheftEvent>(
        thief, victim, stolenItem, quantity, theftLocation);
    EventManager::Instance().dispatchEvent(theftEvent, EventManager::DispatchMode::Immediate);

    const auto& rtm = ResourceTemplateManager::Instance();
    auto resTemplate = rtm.getResourceTemplate(stolenItem);
    std::string itemName = resTemplate ? resTemplate->getName() : "unknown item";

    SOCIAL_INFO(std::format("Theft reported: {} x{} stolen at ({:.0f}, {:.0f})",
                            itemName, quantity, theftLocation.getX(), theftLocation.getY()));

    alertNearbyGuards(theftLocation, thief);
}

void SocialController::alertNearbyGuards(const Vector2D& location, EntityHandle criminal) {
    (void)criminal;  // Guards detect threats autonomously after alert
    auto& edm = EntityDataManager::Instance();

    // Scan guard index for nearby guards — O(G) not O(N)
    m_nearbyGuardBuffer.clear();
    AIManager::Instance().scanGuardsInRadius(location, GUARD_ALERT_RANGE,
                                             m_nearbyGuardBuffer, true);
    int guardsAlerted = 0;

    for (size_t idx : m_nearbyGuardBuffer) {
        Behaviors::queueBehaviorMessage(idx, BehaviorMessage::RAISE_ALERT);

        ++guardsAlerted;
        SOCIAL_DEBUG(std::format("Guard at ({:.0f}, {:.0f}) alerted to theft",
                                 edm.getHotDataByIndex(idx).transform.position.getX(),
                                 edm.getHotDataByIndex(idx).transform.position.getY()));
    }

    if (guardsAlerted > 0) {
        SOCIAL_INFO(std::format("Alerted {} guards to theft at ({:.0f}, {:.0f})",
                                guardsAlerted, location.getX(), location.getY()));

        UIManager::Instance().addEventLogEntry(
            GAMEPLAY_EVENT_LOG,
            std::format("Guards alerted! {} guards responding to crime.", guardsAlerted));
    }
}

// ============================================================================
// SHARED — Relationship Queries
// ============================================================================

float SocialController::getRelationshipLevel(EntityHandle npcHandle) const {
    auto player = mp_player.lock();
    EntityHandle playerHandle = player ? player->getHandle() : EntityHandle{};
    return Behaviors::getRelationshipLevel(npcHandle, playerHandle);
}

float SocialController::getPriceModifier(EntityHandle npcHandle) const {
    float relationship = getRelationshipLevel(npcHandle);
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
// SHARED — NPC Inventory Helpers
// ============================================================================

bool SocialController::isMerchant(EntityHandle npcHandle) const {
    return EntityDataManager::Instance().isNPCMerchant(npcHandle);
}

uint32_t SocialController::getNPCInventoryIndex(EntityHandle npcHandle) const {
    return EntityDataManager::Instance().getNPCInventoryIndex(npcHandle);
}

// ============================================================================
// PRIVATE — Memory Recording
// ============================================================================

void SocialController::recordTrade(EntityHandle npcHandle, float tradeValue, bool wasGoodDeal) {
    float value = wasGoodDeal ? (tradeValue * 0.01f) : -(tradeValue * 0.005f);
    recordInteraction(npcHandle, InteractionType::Trade, value);
}

void SocialController::recordGift(EntityHandle npcHandle, float giftValue) {
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
            suspicion = -0.05f * (value > 0 ? 1.0f : -0.5f);
            break;
        case InteractionType::Gift:
            suspicion = -0.15f;
            aggression = -0.1f;
            fear = -0.05f;
            break;
        case InteractionType::Greeting:
            suspicion = -0.02f;
            break;
        case InteractionType::Help:
            suspicion = -0.2f;
            aggression = -0.15f;
            fear = -0.1f;
            break;
        case InteractionType::Theft:
            suspicion = 0.4f;
            aggression = 0.3f;
            fear = 0.1f;
            break;
        case InteractionType::Insult:
            aggression = 0.2f;
            suspicion = 0.15f;
            break;
    }

    edm.modifyEmotions(idx, aggression, fear, curiosity, suspicion);
}

float SocialController::getItemBaseValue(VoidLight::ResourceHandle itemHandle) const {
    if (!itemHandle.isValid()) {
        return 0.0f;
    }

    return ResourceTemplateManager::Instance().getValue(itemHandle);
}

// ============================================================================
// PRIVATE — Trade UI Management
// ============================================================================

void SocialController::createTradeUI() {
    auto& ui = UIManager::Instance();

    constexpr int panelW = 600;
    constexpr int panelH = 450;
    constexpr int halfW = panelW / 2;
    constexpr int halfH = panelH / 2;

    ui.createPanel(UI_PANEL, UIRect{0, 0, panelW, panelH});
    ui.setComponentPositioning(UI_PANEL, {UIPositionMode::CENTERED_BOTH, 0, 0, panelW, panelH});

    ui.createTitle(UI_TITLE, UIRect{0, 0, 560, 30}, "Trading");
    ui.setComponentPositioning(UI_TITLE, {UIPositionMode::CENTERED_BOTH, 0, 10 + 15 - halfH, 560, 30});

    std::string relStr = std::format("Relationship: {}  (Price: {:.0f}%)",
                                     getCurrentTradeRelationshipDescription(),
                                     getCurrentTradePriceModifier() * 100.0f);
    ui.createLabel(UI_RELATIONSHIP, UIRect{0, 0, 560, 20}, relStr);
    ui.setComponentPositioning(UI_RELATIONSHIP, {UIPositionMode::CENTERED_BOTH, 0, 45 + 10 - halfH, 560, 20});

    ui.createLabel("trade_merchant_label", UIRect{0, 0, 270, 20}, "Merchant Inventory");
    ui.setComponentPositioning("trade_merchant_label", {UIPositionMode::CENTERED_BOTH,
        20 + 135 - halfW, 75 + 10 - halfH, 270, 20});

    ui.createList(UI_MERCHANT_LIST, UIRect{0, 0, 270, 200});
    ui.setComponentPositioning(UI_MERCHANT_LIST, {UIPositionMode::CENTERED_BOTH,
        20 + 135 - halfW, 100 + 100 - halfH, 270, 200});

    ui.createLabel("trade_player_label", UIRect{0, 0, 270, 20}, "Your Inventory");
    ui.setComponentPositioning("trade_player_label", {UIPositionMode::CENTERED_BOTH,
        310 + 135 - halfW, 75 + 10 - halfH, 270, 20});

    ui.createList(UI_PLAYER_LIST, UIRect{0, 0, 270, 200});
    ui.setComponentPositioning(UI_PLAYER_LIST, {UIPositionMode::CENTERED_BOTH,
        310 + 135 - halfW, 100 + 100 - halfH, 270, 200});

    for (const auto& item : m_merchantItems) {
        std::string itemStr = std::format("{} x{} ({:.0f}g)", item.name, item.quantity, item.unitPrice);
        ui.addListItem(UI_MERCHANT_LIST, itemStr);
    }

    for (const auto& item : m_playerItems) {
        std::string itemStr = std::format("{} x{}", item.name, item.quantity);
        ui.addListItem(UI_PLAYER_LIST, itemStr);
    }

    ui.createLabel(UI_QUANTITY_LABEL, UIRect{0, 0, 150, 25}, "Quantity: 1");
    ui.setComponentPositioning(UI_QUANTITY_LABEL, {UIPositionMode::CENTERED_BOTH,
        20 + 75 - halfW, 320 + 12 - halfH, 150, 25});

    ui.createLabel(UI_PRICE_LABEL, UIRect{0, 0, 200, 25}, "Select an item");
    ui.setComponentPositioning(UI_PRICE_LABEL, {UIPositionMode::CENTERED_BOTH,
        180 + 100 - halfW, 320 + 12 - halfH, 200, 25});

    auto player = mp_player.lock();
    int gold = player ? player->getGold() : 0;
    ui.createLabel(UI_GOLD_LABEL, UIRect{0, 0, 180, 25}, std::format("Your Gold: {}", gold));
    ui.setComponentPositioning(UI_GOLD_LABEL, {UIPositionMode::CENTERED_BOTH,
        400 + 90 - halfW, 320 + 12 - halfH, 180, 25});

    constexpr int btnY = 360;
    constexpr int btnW = 100;
    constexpr int btnH = 35;

    ui.createButtonSuccess(UI_BUY_BTN, UIRect{0, 0, btnW, btnH}, "Buy");
    ui.setComponentPositioning(UI_BUY_BTN, {UIPositionMode::CENTERED_BOTH,
        -175, btnY + btnH/2 - halfH, btnW, btnH});
    ui.setOnClick(UI_BUY_BTN, [this]() {
        executeBuy();
    });

    ui.createButtonSuccess(UI_SELL_BTN, UIRect{0, 0, btnW, btnH}, "Sell");
    ui.setComponentPositioning(UI_SELL_BTN, {UIPositionMode::CENTERED_BOTH,
        0, btnY + btnH/2 - halfH, btnW, btnH});
    ui.setOnClick(UI_SELL_BTN, [this]() {
        executeSell();
    });

    ui.createButtonDanger(UI_CLOSE_BTN, UIRect{0, 0, btnW, btnH}, "Close");
    ui.setComponentPositioning(UI_CLOSE_BTN, {UIPositionMode::CENTERED_BOTH,
        175, btnY + btnH/2 - halfH, btnW, btnH});
    ui.setOnClick(UI_CLOSE_BTN, [this]() {
        closeTrade();
    });

    ui.setOnClick(UI_MERCHANT_LIST, [this]() {
        const auto& uiMgr = UIManager::Instance();
        int idx = uiMgr.getSelectedListItem(UI_MERCHANT_LIST);
        if (idx >= 0) {
            selectMerchantItem(static_cast<size_t>(idx));
        }
    });

    ui.setOnClick(UI_PLAYER_LIST, [this]() {
        const auto& uiMgr = UIManager::Instance();
        int idx = uiMgr.getSelectedListItem(UI_PLAYER_LIST);
        if (idx >= 0) {
            selectPlayerItem(static_cast<size_t>(idx));
        }
    });
}

void SocialController::destroyTradeUI() {
    auto& ui = UIManager::Instance();
    ui.removeComponentsWithPrefix("trade_");
}

void SocialController::refreshMerchantItems() {
    m_merchantItems.clear();

    const auto& edm = EntityDataManager::Instance();
    const auto& rtm = ResourceTemplateManager::Instance();

    uint32_t invIdx = getNPCInventoryIndex(m_merchantHandle);
    if (invIdx == INVALID_INVENTORY_INDEX) {
        return;
    }

    auto resources = edm.getInventoryResources(invIdx);
    for (const auto& [handle, qty] : resources) {
        if (qty <= 0) continue;

        TradeItemInfo info;
        info.handle = handle;
        info.quantity = qty;
        auto resTemplate = rtm.getResourceTemplate(handle);
        info.name = resTemplate ? resTemplate->getName() : "Unknown";
        info.unitPrice = calculateBuyPrice(m_merchantHandle, handle, 1);

        m_merchantItems.push_back(info);
    }

    m_priceDisplayDirty = true;
}

void SocialController::refreshPlayerItems() {
    m_playerItems.clear();

    auto player = mp_player.lock();
    if (!player) {
        return;
    }

    const auto& edm = EntityDataManager::Instance();
    const auto& rtm = ResourceTemplateManager::Instance();

    uint32_t invIdx = player->getInventoryIndex();
    if (invIdx == INVALID_INVENTORY_INDEX) {
        return;
    }

    auto goldHandle = rtm.getHandleByName("gold");
    auto resources = edm.getInventoryResources(invIdx);
    for (const auto& [handle, qty] : resources) {
        if (qty <= 0) continue;
        if (handle == goldHandle) continue;

        TradeItemInfo info;
        info.handle = handle;
        info.quantity = qty;
        auto resTemplate = rtm.getResourceTemplate(handle);
        info.name = resTemplate ? resTemplate->getName() : "Unknown";
        info.unitPrice = calculateSellPrice(m_merchantHandle, handle, 1);

        m_playerItems.push_back(info);
    }

    m_priceDisplayDirty = true;
}

void SocialController::updatePriceDisplay() {
    if (!m_isTrading) {
        return;
    }

    if (!m_priceDisplayDirty) {
        return;
    }

    m_priceDisplayDirty = false;

    auto& ui = UIManager::Instance();

    ui.setText(UI_QUANTITY_LABEL, std::format("Quantity: {}", m_quantity));

    if (m_selectedMerchantIndex >= 0) {
        float price = getCurrentBuyPrice();
        ui.setText(UI_PRICE_LABEL, std::format("Buy Price: {:.0f} gold", price));
    } else if (m_selectedPlayerIndex >= 0) {
        float price = getCurrentSellPrice();
        ui.setText(UI_PRICE_LABEL, std::format("Sell Price: {:.0f} gold", price));
    } else {
        ui.setText(UI_PRICE_LABEL, "Select an item");
    }

    auto player = mp_player.lock();
    int gold = player ? player->getGold() : 0;
    ui.setText(UI_GOLD_LABEL, std::format("Your Gold: {}", gold));
}

void SocialController::updateSelectionHighlight() {
    // Future: highlight selected items in lists
}
