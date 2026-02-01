/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/social/TradeController.hpp"
#include "controllers/social/SocialController.hpp"
#include "core/Logger.hpp"
#include "entities/Player.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/UIManager.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace {
    constexpr const char* GAMEPLAY_EVENT_LOG = "gameplay_event_log";
}

// ============================================================================
// CONSTRUCTION
// ============================================================================

TradeController::TradeController(std::shared_ptr<Player> player,
                                 SocialController* socialController)
    : mp_player(std::move(player))
    , mp_socialController(socialController) {}

void TradeController::subscribe() {
    if (checkAlreadySubscribed()) {
        return;
    }

    // TradeController doesn't subscribe to events currently
    // It's driven by player interaction (openTrade/closeTrade)

    setSubscribed(true);
    TRADE_INFO("TradeController subscribed");
}

// ============================================================================
// TRADE SESSION
// ============================================================================

bool TradeController::openTrade(EntityHandle npcHandle) {
    if (m_isTrading) {
        TRADE_DEBUG("Already in trade session");
        return false;
    }

    auto player = mp_player.lock();
    if (!player) {
        TRADE_ERROR("Player reference lost");
        return false;
    }

    // Validate NPC is a merchant
    if (!isMerchant(npcHandle)) {
        TRADE_DEBUG("NPC is not a merchant");
        return false;
    }

    // Check if NPC will refuse trade
    if (willRefuseTrade(npcHandle)) {
        TRADE_INFO("Merchant refused to trade (relationship too low)");
        return false;
    }

    m_merchantHandle = npcHandle;
    m_isTrading = true;
    m_selectedMerchantIndex = -1;
    m_selectedPlayerIndex = -1;
    m_quantity = 1;
    m_priceDisplayDirty = true;

    // Refresh item lists
    refreshMerchantItems();
    refreshPlayerItems();

    // Create UI
    createTradeUI();

    TRADE_INFO("Trade session opened");
    return true;
}

void TradeController::closeTrade() {
    if (!m_isTrading) {
        TRADE_DEBUG("closeTrade called but m_isTrading is false");
        return;
    }

    TRADE_INFO("closeTrade executing - destroying UI");
    destroyTradeUI();

    m_isTrading = false;
    TRADE_INFO("Trade closed - m_isTrading now false");
    m_merchantHandle = EntityHandle{};
    m_merchantItems.clear();
    m_playerItems.clear();
    m_selectedMerchantIndex = -1;
    m_selectedPlayerIndex = -1;
    m_quantity = 1;
    m_priceDisplayDirty = true;

    TRADE_INFO("Trade session closed");
}

// ============================================================================
// UI UPDATE
// ============================================================================

void TradeController::update([[maybe_unused]] float deltaTime) {
    if (!m_isTrading) {
        return;
    }

    // Refresh displays periodically or on changes
    updatePriceDisplay();
}

// ============================================================================
// SELECTION & TRANSACTIONS
// ============================================================================

void TradeController::selectMerchantItem(size_t index) {
    if (index < m_merchantItems.size()) {
        m_selectedMerchantIndex = static_cast<int>(index);
        m_selectedPlayerIndex = -1;  // Deselect player item
        m_quantity = 1;
        m_priceDisplayDirty = true;
        TRADE_DEBUG(std::format("Selected merchant item: {}", m_merchantItems[index].name));
    }
}

void TradeController::selectPlayerItem(size_t index) {
    if (index < m_playerItems.size()) {
        m_selectedPlayerIndex = static_cast<int>(index);
        m_selectedMerchantIndex = -1;  // Deselect merchant item
        m_quantity = 1;
        m_priceDisplayDirty = true;
        TRADE_DEBUG(std::format("Selected player item: {}", m_playerItems[index].name));
    }
}

void TradeController::setQuantity(int qty) {
    m_quantity = std::max(1, qty);

    // Clamp to available quantity
    if (m_selectedMerchantIndex >= 0 &&
        static_cast<size_t>(m_selectedMerchantIndex) < m_merchantItems.size()) {
        m_quantity = std::min(m_quantity, m_merchantItems[m_selectedMerchantIndex].quantity);
    } else if (m_selectedPlayerIndex >= 0 &&
               static_cast<size_t>(m_selectedPlayerIndex) < m_playerItems.size()) {
        m_quantity = std::min(m_quantity, m_playerItems[m_selectedPlayerIndex].quantity);
    }

    m_priceDisplayDirty = true;
}

TradeResult TradeController::executeBuy() {
    if (!m_isTrading || m_selectedMerchantIndex < 0) {
        return TradeResult::InvalidItem;
    }

    if (static_cast<size_t>(m_selectedMerchantIndex) >= m_merchantItems.size()) {
        return TradeResult::InvalidItem;
    }

    if (!mp_socialController) {
        TRADE_ERROR("SocialController not available for trade execution");
        return TradeResult::InvalidNPC;
    }

    const auto& item = m_merchantItems[m_selectedMerchantIndex];
    TradeResult result = mp_socialController->tryBuy(m_merchantHandle, item.handle, m_quantity);

    if (result == TradeResult::Success) {
        // Get price for log message before resetting quantity
        float price = calculateBuyPrice(m_merchantHandle, item.handle, m_quantity);
        int savedQty = m_quantity;

        // Refresh both inventories
        refreshMerchantItems();
        refreshPlayerItems();
        m_selectedMerchantIndex = -1;
        m_quantity = 1;
        m_priceDisplayDirty = true;
        TRADE_INFO(std::format("Bought {} x{}", item.name, savedQty));

        // Add to on-screen event log
        UIManager::Instance().addEventLogEntry(
            GAMEPLAY_EVENT_LOG,
            std::format("Bought {} x{} for {:.0f} gold", item.name, savedQty, price));
    }

    return result;
}

TradeResult TradeController::executeSell() {
    if (!m_isTrading || m_selectedPlayerIndex < 0) {
        return TradeResult::InvalidItem;
    }

    if (static_cast<size_t>(m_selectedPlayerIndex) >= m_playerItems.size()) {
        return TradeResult::InvalidItem;
    }

    if (!mp_socialController) {
        TRADE_ERROR("SocialController not available for trade execution");
        return TradeResult::InvalidNPC;
    }

    const auto& item = m_playerItems[m_selectedPlayerIndex];
    TradeResult result = mp_socialController->trySell(m_merchantHandle, item.handle, m_quantity);

    if (result == TradeResult::Success) {
        // Get price for log message before resetting quantity
        float price = calculateSellPrice(m_merchantHandle, item.handle, m_quantity);
        int savedQty = m_quantity;

        // Refresh both inventories
        refreshMerchantItems();
        refreshPlayerItems();
        m_selectedPlayerIndex = -1;
        m_quantity = 1;
        m_priceDisplayDirty = true;
        TRADE_INFO(std::format("Sold {} x{}", item.name, savedQty));

        // Add to on-screen event log
        UIManager::Instance().addEventLogEntry(
            GAMEPLAY_EVENT_LOG,
            std::format("Sold {} x{} for {:.0f} gold", item.name, savedQty, price));
    }

    return result;
}

// ============================================================================
// ACCESSORS
// ============================================================================

float TradeController::getCurrentBuyPrice() const {
    if (m_selectedMerchantIndex < 0 ||
        static_cast<size_t>(m_selectedMerchantIndex) >= m_merchantItems.size()) {
        return 0.0f;
    }

    const auto& item = m_merchantItems[m_selectedMerchantIndex];
    return calculateBuyPrice(m_merchantHandle, item.handle, m_quantity);
}

float TradeController::getCurrentSellPrice() const {
    if (m_selectedPlayerIndex < 0 ||
        static_cast<size_t>(m_selectedPlayerIndex) >= m_playerItems.size()) {
        return 0.0f;
    }

    const auto& item = m_playerItems[m_selectedPlayerIndex];
    return calculateSellPrice(m_merchantHandle, item.handle, m_quantity);
}

std::string TradeController::getRelationshipDescription() const {
    if (!m_isTrading) {
        return "N/A";
    }
    return getRelationshipDescriptionInternal(m_merchantHandle);
}

float TradeController::getPriceModifier() const {
    if (!m_isTrading) {
        return 1.0f;
    }
    return getPriceModifierInternal(m_merchantHandle);
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void TradeController::createTradeUI() {
    auto& ui = UIManager::Instance();

    // Panel dimensions - all child offsets calculated from panel center
    // Panel: 600x450, so half = 300x225
    // Child offset formula: offsetX = elemX + elemW/2 - 300, offsetY = elemY + elemH/2 - 225
    constexpr int panelW = 600;
    constexpr int panelH = 450;
    constexpr int halfW = panelW / 2;  // 300
    constexpr int halfH = panelH / 2;  // 225

    // Create main panel (centered)
    ui.createPanel(UI_PANEL, UIRect{0, 0, panelW, panelH});
    ui.setComponentPositioning(UI_PANEL, {UIPositionMode::CENTERED_BOTH, 0, 0, panelW, panelH});

    // Title (centered horizontally at top of panel)
    ui.createTitle(UI_TITLE, UIRect{0, 0, 560, 30}, "Trading");
    ui.setComponentPositioning(UI_TITLE, {UIPositionMode::CENTERED_BOTH, 0, 10 + 15 - halfH, 560, 30});

    // Relationship info
    std::string relStr = std::format("Relationship: {}  (Price: {:.0f}%)",
                                     getRelationshipDescription(),
                                     getPriceModifier() * 100.0f);
    ui.createLabel(UI_RELATIONSHIP, UIRect{0, 0, 560, 20}, relStr);
    ui.setComponentPositioning(UI_RELATIONSHIP, {UIPositionMode::CENTERED_BOTH, 0, 45 + 10 - halfH, 560, 20});

    // Merchant inventory list (left side)
    ui.createLabel("trade_merchant_label", UIRect{0, 0, 270, 20}, "Merchant Inventory");
    ui.setComponentPositioning("trade_merchant_label", {UIPositionMode::CENTERED_BOTH,
        20 + 135 - halfW, 75 + 10 - halfH, 270, 20});

    ui.createList(UI_MERCHANT_LIST, UIRect{0, 0, 270, 200});
    ui.setComponentPositioning(UI_MERCHANT_LIST, {UIPositionMode::CENTERED_BOTH,
        20 + 135 - halfW, 100 + 100 - halfH, 270, 200});

    // Player inventory list (right side)
    ui.createLabel("trade_player_label", UIRect{0, 0, 270, 20}, "Your Inventory");
    ui.setComponentPositioning("trade_player_label", {UIPositionMode::CENTERED_BOTH,
        310 + 135 - halfW, 75 + 10 - halfH, 270, 20});

    ui.createList(UI_PLAYER_LIST, UIRect{0, 0, 270, 200});
    ui.setComponentPositioning(UI_PLAYER_LIST, {UIPositionMode::CENTERED_BOTH,
        310 + 135 - halfW, 100 + 100 - halfH, 270, 200});

    // Populate lists
    for (const auto& item : m_merchantItems) {
        std::string itemStr = std::format("{} x{} ({:.0f}g)", item.name, item.quantity, item.unitPrice);
        ui.addListItem(UI_MERCHANT_LIST, itemStr);
    }

    for (const auto& item : m_playerItems) {
        std::string itemStr = std::format("{} x{}", item.name, item.quantity);
        ui.addListItem(UI_PLAYER_LIST, itemStr);
    }

    // Quantity and price row
    ui.createLabel(UI_QUANTITY_LABEL, UIRect{0, 0, 150, 25}, "Quantity: 1");
    ui.setComponentPositioning(UI_QUANTITY_LABEL, {UIPositionMode::CENTERED_BOTH,
        20 + 75 - halfW, 320 + 12 - halfH, 150, 25});

    ui.createLabel(UI_PRICE_LABEL, UIRect{0, 0, 200, 25}, "Select an item");
    ui.setComponentPositioning(UI_PRICE_LABEL, {UIPositionMode::CENTERED_BOTH,
        180 + 100 - halfW, 320 + 12 - halfH, 200, 25});

    // Gold display
    auto player = mp_player.lock();
    int gold = player ? player->getGold() : 0;
    ui.createLabel(UI_GOLD_LABEL, UIRect{0, 0, 180, 25}, std::format("Your Gold: {}", gold));
    ui.setComponentPositioning(UI_GOLD_LABEL, {UIPositionMode::CENTERED_BOTH,
        400 + 90 - halfW, 320 + 12 - halfH, 180, 25});

    // Action buttons - evenly spaced across panel
    // Panel 600 wide, 3 buttons of 100 each, gaps of 75: positions at 75, 250, 425
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

    // Set up list selection callbacks
    ui.setOnClick(UI_MERCHANT_LIST, [this]() {
        auto& uiMgr = UIManager::Instance();
        int idx = uiMgr.getSelectedListItem(UI_MERCHANT_LIST);
        if (idx >= 0) {
            selectMerchantItem(static_cast<size_t>(idx));
        }
    });

    ui.setOnClick(UI_PLAYER_LIST, [this]() {
        auto& uiMgr = UIManager::Instance();
        int idx = uiMgr.getSelectedListItem(UI_PLAYER_LIST);
        if (idx >= 0) {
            selectPlayerItem(static_cast<size_t>(idx));
        }
    });
}

void TradeController::destroyTradeUI() {
    auto& ui = UIManager::Instance();
    ui.removeComponentsWithPrefix("trade_");
}

void TradeController::refreshMerchantItems() {
    m_merchantItems.clear();

    auto& edm = EntityDataManager::Instance();
    auto& rtm = ResourceTemplateManager::Instance();

    uint32_t invIdx = getNPCInventoryIndex(m_merchantHandle);
    if (invIdx == INVALID_INVENTORY_INDEX) {
        return;
    }

    // Get all items in merchant inventory
    auto resources = edm.getInventoryResources(invIdx);
    for (const auto& [handle, qty] : resources) {
        if (qty <= 0) continue;

        TradeItemInfo info;
        info.handle = handle;
        info.quantity = qty;
        // Get name from resource template
        auto resTemplate = rtm.getResourceTemplate(handle);
        info.name = resTemplate ? resTemplate->getName() : "Unknown";
        info.unitPrice = calculateBuyPrice(m_merchantHandle, handle, 1);

        m_merchantItems.push_back(info);
    }

    m_priceDisplayDirty = true;
}

void TradeController::refreshPlayerItems() {
    m_playerItems.clear();

    auto player = mp_player.lock();
    if (!player) {
        return;
    }

    auto& edm = EntityDataManager::Instance();
    auto& rtm = ResourceTemplateManager::Instance();

    uint32_t invIdx = player->getInventoryIndex();
    if (invIdx == INVALID_INVENTORY_INDEX) {
        return;
    }

    // Get all items in player inventory (excluding gold for cleaner display)
    auto goldHandle = rtm.getHandleByName("gold");
    auto resources = edm.getInventoryResources(invIdx);
    for (const auto& [handle, qty] : resources) {
        if (qty <= 0) continue;
        if (handle == goldHandle) continue;  // Don't show gold in tradeable items

        TradeItemInfo info;
        info.handle = handle;
        info.quantity = qty;
        // Get name from resource template
        auto resTemplate = rtm.getResourceTemplate(handle);
        info.name = resTemplate ? resTemplate->getName() : "Unknown";
        info.unitPrice = calculateSellPrice(m_merchantHandle, handle, 1);

        m_playerItems.push_back(info);
    }

    m_priceDisplayDirty = true;
}

void TradeController::updatePriceDisplay() {
    if (!m_isTrading) {
        return;
    }

    if (!m_priceDisplayDirty) {
        return;
    }

    m_priceDisplayDirty = false;

    auto& ui = UIManager::Instance();

    // Update quantity label
    ui.setText(UI_QUANTITY_LABEL, std::format("Quantity: {}", m_quantity));

    // Update price label based on selection
    if (m_selectedMerchantIndex >= 0) {
        float price = getCurrentBuyPrice();
        ui.setText(UI_PRICE_LABEL, std::format("Buy Price: {:.0f} gold", price));
    } else if (m_selectedPlayerIndex >= 0) {
        float price = getCurrentSellPrice();
        ui.setText(UI_PRICE_LABEL, std::format("Sell Price: {:.0f} gold", price));
    } else {
        ui.setText(UI_PRICE_LABEL, "Select an item");
    }

    // Update gold display
    auto player = mp_player.lock();
    int gold = player ? player->getGold() : 0;
    ui.setText(UI_GOLD_LABEL, std::format("Your Gold: {}", gold));
}

void TradeController::updateSelectionHighlight() {
    // Future: highlight selected items in lists
}

// ============================================================================
// HELPER METHODS (Manager access, replacing SocialController dependency)
// ============================================================================

bool TradeController::isMerchant(EntityHandle npcHandle) const {
    return EntityDataManager::Instance().isNPCMerchant(npcHandle);
}

bool TradeController::willRefuseTrade(EntityHandle npcHandle) const {
    float relationship = getRelationshipLevel(npcHandle);
    constexpr float RELATIONSHIP_HOSTILE = -0.5f;
    return relationship < RELATIONSHIP_HOSTILE;
}

float TradeController::getRelationshipLevel(EntityHandle npcHandle) const {
    constexpr float RELATIONSHIP_NEUTRAL = 0.0f;

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

uint32_t TradeController::getNPCInventoryIndex(EntityHandle npcHandle) const {
    return EntityDataManager::Instance().getNPCInventoryIndex(npcHandle);
}

float TradeController::calculateBuyPrice(EntityHandle npcHandle,
                                         HammerEngine::ResourceHandle itemHandle,
                                         int quantity) const {
    constexpr float BUY_PRICE_MULTIPLIER = 1.2f;
    float baseValue = getItemBaseValue(itemHandle);
    float modifier = getPriceModifierInternal(npcHandle);

    return baseValue * modifier * BUY_PRICE_MULTIPLIER * quantity;
}

float TradeController::calculateSellPrice(EntityHandle npcHandle,
                                          HammerEngine::ResourceHandle itemHandle,
                                          int quantity) const {
    constexpr float SELL_PRICE_MULTIPLIER = 0.6f;
    float baseValue = getItemBaseValue(itemHandle);
    float modifier = getPriceModifierInternal(npcHandle);

    // Better relationship = better sell price (inverse of buy modifier)
    float sellModifier = 2.0f - modifier;  // If buy is 0.7x, sell becomes 1.3x

    return baseValue * sellModifier * SELL_PRICE_MULTIPLIER * quantity;
}

std::string TradeController::getRelationshipDescriptionInternal(EntityHandle npcHandle) const {
    constexpr float RELATIONSHIP_TRUSTED = 0.5f;
    constexpr float RELATIONSHIP_FRIENDLY = 0.25f;
    constexpr float RELATIONSHIP_NEUTRAL = 0.0f;
    constexpr float RELATIONSHIP_UNFRIENDLY = -0.25f;
    constexpr float RELATIONSHIP_HOSTILE = -0.5f;

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

float TradeController::getPriceModifierInternal(EntityHandle npcHandle) const {
    float relationship = getRelationshipLevel(npcHandle);

    // Map relationship [-1, 1] to price modifier [1.3, 0.7]
    // -1.0 (hostile) -> 1.3x prices (30% more expensive)
    //  0.0 (neutral) -> 1.0x prices (normal)
    // +1.0 (trusted) -> 0.7x prices (30% cheaper)

    return 1.0f - (relationship * 0.3f);
}

float TradeController::getItemBaseValue(HammerEngine::ResourceHandle itemHandle) const {
    if (!itemHandle.isValid()) {
        return 0.0f;
    }

    return ResourceTemplateManager::Instance().getValue(itemHandle);
}
