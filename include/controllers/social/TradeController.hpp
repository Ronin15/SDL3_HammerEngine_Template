/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef TRADE_CONTROLLER_HPP
#define TRADE_CONTROLLER_HPP

/**
 * @file TradeController.hpp
 * @brief Controller for managing the trading UI between player and NPC merchants
 *
 * TradeController handles:
 * - Opening/closing trade interface when player interacts with merchant
 * - Displaying merchant and player inventories side by side
 * - Buy/sell transactions via SocialController
 * - Price display based on relationship modifier
 *
 * Ownership: ControllerRegistry owns the controller instance.
 */

#include "controllers/ControllerBase.hpp"
#include "controllers/IUpdatable.hpp"
#include "controllers/social/SocialController.hpp"
#include "entities/EntityHandle.hpp"
#include "utils/ResourceHandle.hpp"
#include <memory>
#include <string>
#include <vector>

// Forward declarations
class Player;

/**
 * @brief Item display info for UI lists
 */
struct TradeItemInfo {
    HammerEngine::ResourceHandle handle;
    std::string name;
    int quantity{0};
    float unitPrice{0.0f};
};

class TradeController : public ControllerBase, public IUpdatable {
public:
    /**
     * @brief Construct TradeController with required player and social controller references
     * @param player Shared pointer to the player (required)
     * @param socialController Reference to SocialController for trade logic
     */
    explicit TradeController(std::shared_ptr<Player> player,
                             SocialController& socialController);

    ~TradeController() override = default;

    // Non-copyable, non-movable (has reference member)
    TradeController(const TradeController&) = delete;
    TradeController& operator=(const TradeController&) = delete;
    TradeController(TradeController&&) = delete;
    TradeController& operator=(TradeController&&) = delete;

    // --- ControllerBase interface ---

    void subscribe() override;

    [[nodiscard]] std::string_view getName() const override { return "TradeController"; }

    // ========================================================================
    // TRADE SESSION
    // ========================================================================

    /**
     * @brief Open trade interface with an NPC merchant
     * @param npcHandle Handle to the merchant NPC
     * @return true if trade opened successfully
     */
    bool openTrade(EntityHandle npcHandle);

    /**
     * @brief Close the current trade interface
     */
    void closeTrade();

    /**
     * @brief Check if currently in a trade session
     */
    [[nodiscard]] bool isTrading() const { return m_isTrading; }

    /**
     * @brief Get the current merchant handle
     */
    [[nodiscard]] EntityHandle getMerchantHandle() const { return m_merchantHandle; }

    // ========================================================================
    // UI UPDATE
    // ========================================================================

    /**
     * @brief Update trade UI (call each frame while trading)
     * @param deltaTime Frame delta time
     */
    void update(float deltaTime);

    // ========================================================================
    // SELECTION & TRANSACTIONS
    // ========================================================================

    /**
     * @brief Select an item from merchant inventory for buying
     * @param index Index in merchant item list
     */
    void selectMerchantItem(size_t index);

    /**
     * @brief Select an item from player inventory for selling
     * @param index Index in player item list
     */
    void selectPlayerItem(size_t index);

    /**
     * @brief Set quantity for current transaction
     * @param qty Quantity to buy/sell
     */
    void setQuantity(int qty);

    /**
     * @brief Execute buy transaction for selected merchant item
     * @return TradeResult indicating success or failure
     */
    TradeResult executeBuy();

    /**
     * @brief Execute sell transaction for selected player item
     * @return TradeResult indicating success or failure
     */
    TradeResult executeSell();

    // ========================================================================
    // ACCESSORS
    // ========================================================================

    /**
     * @brief Get merchant's tradeable items
     */
    [[nodiscard]] const std::vector<TradeItemInfo>& getMerchantItems() const { return m_merchantItems; }

    /**
     * @brief Get player's tradeable items
     */
    [[nodiscard]] const std::vector<TradeItemInfo>& getPlayerItems() const { return m_playerItems; }

    /**
     * @brief Get current transaction quantity
     */
    [[nodiscard]] int getQuantity() const { return m_quantity; }

    /**
     * @brief Get selected merchant item index (-1 if none)
     */
    [[nodiscard]] int getSelectedMerchantIndex() const { return m_selectedMerchantIndex; }

    /**
     * @brief Get selected player item index (-1 if none)
     */
    [[nodiscard]] int getSelectedPlayerIndex() const { return m_selectedPlayerIndex; }

    /**
     * @brief Get current buy price for selected item and quantity
     */
    [[nodiscard]] float getCurrentBuyPrice() const;

    /**
     * @brief Get current sell price for selected item and quantity
     */
    [[nodiscard]] float getCurrentSellPrice() const;

    /**
     * @brief Get relationship description with current merchant
     */
    [[nodiscard]] std::string getRelationshipDescription() const;

    /**
     * @brief Get price modifier with current merchant
     */
    [[nodiscard]] float getPriceModifier() const;

private:
    void createTradeUI();
    void destroyTradeUI();
    void refreshMerchantItems();
    void refreshPlayerItems();
    void updatePriceDisplay();
    void updateSelectionHighlight();

    // References
    std::weak_ptr<Player> mp_player;
    SocialController& m_socialController;

    // Trade state
    EntityHandle m_merchantHandle;
    bool m_isTrading{false};

    // Item lists
    std::vector<TradeItemInfo> m_merchantItems;
    std::vector<TradeItemInfo> m_playerItems;

    // Selection state
    int m_selectedMerchantIndex{-1};
    int m_selectedPlayerIndex{-1};
    int m_quantity{1};

    // UI element IDs
    static constexpr const char* UI_PANEL = "trade_panel";
    static constexpr const char* UI_TITLE = "trade_title";
    static constexpr const char* UI_RELATIONSHIP = "trade_relationship";
    static constexpr const char* UI_MERCHANT_LIST = "trade_merchant_list";
    static constexpr const char* UI_PLAYER_LIST = "trade_player_list";
    static constexpr const char* UI_QUANTITY_LABEL = "trade_qty_label";
    static constexpr const char* UI_PRICE_LABEL = "trade_price_label";
    static constexpr const char* UI_BUY_BTN = "trade_buy_btn";
    static constexpr const char* UI_SELL_BTN = "trade_sell_btn";
    static constexpr const char* UI_CLOSE_BTN = "trade_close_btn";
    static constexpr const char* UI_GOLD_LABEL = "trade_gold_label";
};

#endif // TRADE_CONTROLLER_HPP
