/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/social/TradeController.hpp"
#include "core/Logger.hpp"
#include "entities/Player.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/UIManager.hpp"
#include <algorithm>
#include <format>

namespace {
    constexpr const char* GAMEPLAY_EVENT_LOG = "gameplay_event_log";
}

// ============================================================================
// CONSTRUCTION
// ============================================================================

TradeController::TradeController(std::shared_ptr<Player> player,
                                 SocialController& socialController)
    : mp_player(std::move(player))
    , m_socialController(socialController) {}

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
    if (!m_socialController.isMerchant(npcHandle)) {
        TRADE_DEBUG("NPC is not a merchant");
        return false;
    }

    // Check if NPC will refuse trade
    if (m_socialController.willRefuseTrade(npcHandle)) {
        TRADE_INFO("Merchant refused to trade (relationship too low)");
        return false;
    }

    m_merchantHandle = npcHandle;
    m_isTrading = true;
    m_selectedMerchantIndex = -1;
    m_selectedPlayerIndex = -1;
    m_quantity = 1;

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
        return;
    }

    destroyTradeUI();

    m_isTrading = false;
    m_merchantHandle = EntityHandle{};
    m_merchantItems.clear();
    m_playerItems.clear();
    m_selectedMerchantIndex = -1;
    m_selectedPlayerIndex = -1;
    m_quantity = 1;

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
        updatePriceDisplay();
        TRADE_DEBUG(std::format("Selected merchant item: {}", m_merchantItems[index].name));
    }
}

void TradeController::selectPlayerItem(size_t index) {
    if (index < m_playerItems.size()) {
        m_selectedPlayerIndex = static_cast<int>(index);
        m_selectedMerchantIndex = -1;  // Deselect merchant item
        m_quantity = 1;
        updatePriceDisplay();
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

    updatePriceDisplay();
}

TradeResult TradeController::executeBuy() {
    if (!m_isTrading || m_selectedMerchantIndex < 0) {
        return TradeResult::InvalidItem;
    }

    if (static_cast<size_t>(m_selectedMerchantIndex) >= m_merchantItems.size()) {
        return TradeResult::InvalidItem;
    }

    const auto& item = m_merchantItems[m_selectedMerchantIndex];
    TradeResult result = m_socialController.tryBuy(m_merchantHandle, item.handle, m_quantity);

    if (result == TradeResult::Success) {
        // Get price for log message before resetting quantity
        float price = m_socialController.calculateBuyPrice(m_merchantHandle, item.handle, m_quantity);
        int savedQty = m_quantity;

        // Refresh both inventories
        refreshMerchantItems();
        refreshPlayerItems();
        m_selectedMerchantIndex = -1;
        m_quantity = 1;
        updatePriceDisplay();
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

    const auto& item = m_playerItems[m_selectedPlayerIndex];
    TradeResult result = m_socialController.trySell(m_merchantHandle, item.handle, m_quantity);

    if (result == TradeResult::Success) {
        // Get price for log message before resetting quantity
        float price = m_socialController.calculateSellPrice(m_merchantHandle, item.handle, m_quantity);
        int savedQty = m_quantity;

        // Refresh both inventories
        refreshMerchantItems();
        refreshPlayerItems();
        m_selectedPlayerIndex = -1;
        m_quantity = 1;
        updatePriceDisplay();
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
    return m_socialController.calculateBuyPrice(m_merchantHandle, item.handle, m_quantity);
}

float TradeController::getCurrentSellPrice() const {
    if (m_selectedPlayerIndex < 0 ||
        static_cast<size_t>(m_selectedPlayerIndex) >= m_playerItems.size()) {
        return 0.0f;
    }

    const auto& item = m_playerItems[m_selectedPlayerIndex];
    return m_socialController.calculateSellPrice(m_merchantHandle, item.handle, m_quantity);
}

std::string TradeController::getRelationshipDescription() const {
    if (!m_isTrading) {
        return "N/A";
    }
    return m_socialController.getRelationshipDescription(m_merchantHandle);
}

float TradeController::getPriceModifier() const {
    if (!m_isTrading) {
        return 1.0f;
    }
    return m_socialController.getPriceModifier(m_merchantHandle);
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void TradeController::createTradeUI() {
    auto& ui = UIManager::Instance();

    // Panel dimensions
    constexpr int panelW = 600;
    constexpr int panelH = 450;
    UIRect panelRect{0, 0, panelW, panelH};

    // Create main panel (centered)
    ui.createPanel(UI_PANEL, panelRect);
    ui.setComponentPositioning(UI_PANEL, {UIPositionMode::CENTERED_BOTH, 0, 0, 0, 0});

    // Title
    ui.createTitle(UI_TITLE, UIRect{20, 10, 560, 30}, "Trading");

    // Relationship info
    std::string relStr = std::format("Relationship: {}  (Price: {:.0f}%)",
                                     getRelationshipDescription(),
                                     getPriceModifier() * 100.0f);
    ui.createLabel(UI_RELATIONSHIP, UIRect{20, 45, 560, 20}, relStr);

    // Merchant inventory list (left side)
    ui.createLabel("trade_merchant_label", UIRect{20, 75, 270, 20}, "Merchant Inventory");
    ui.createList(UI_MERCHANT_LIST, UIRect{20, 100, 270, 200});

    // Player inventory list (right side)
    ui.createLabel("trade_player_label", UIRect{310, 75, 270, 20}, "Your Inventory");
    ui.createList(UI_PLAYER_LIST, UIRect{310, 100, 270, 200});

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
    ui.createLabel(UI_QUANTITY_LABEL, UIRect{20, 320, 150, 25}, "Quantity: 1");
    ui.createLabel(UI_PRICE_LABEL, UIRect{180, 320, 200, 25}, "Select an item");

    // Gold display
    auto player = mp_player.lock();
    int gold = player ? player->getGold() : 0;
    ui.createLabel(UI_GOLD_LABEL, UIRect{400, 320, 180, 25}, std::format("Your Gold: {}", gold));

    // Action buttons
    ui.createButtonSuccess(UI_BUY_BTN, UIRect{20, 360, 100, 35}, "Buy");
    ui.setOnClick(UI_BUY_BTN, [this]() {
        TradeResult result = executeBuy();
        if (result != TradeResult::Success) {
            TRADE_DEBUG(std::format("Buy failed: {}", static_cast<int>(result)));
        }
    });

    ui.createButtonSuccess(UI_SELL_BTN, UIRect{140, 360, 100, 35}, "Sell");
    ui.setOnClick(UI_SELL_BTN, [this]() {
        TradeResult result = executeSell();
        if (result != TradeResult::Success) {
            TRADE_DEBUG(std::format("Sell failed: {}", static_cast<int>(result)));
        }
    });

    ui.createButtonDanger(UI_CLOSE_BTN, UIRect{480, 360, 100, 35}, "Close");
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

    uint32_t invIdx = m_socialController.getNPCInventoryIndex(m_merchantHandle);
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
        info.unitPrice = m_socialController.calculateBuyPrice(m_merchantHandle, handle, 1);

        m_merchantItems.push_back(info);
    }
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
        info.unitPrice = m_socialController.calculateSellPrice(m_merchantHandle, handle, 1);

        m_playerItems.push_back(info);
    }
}

void TradeController::updatePriceDisplay() {
    if (!m_isTrading) {
        return;
    }

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
