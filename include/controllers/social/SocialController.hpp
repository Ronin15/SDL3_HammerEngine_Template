/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef SOCIAL_CONTROLLER_HPP
#define SOCIAL_CONTROLLER_HPP

/**
 * @file SocialController.hpp
 * @brief Controller for NPC social interactions: trading, gifts, and relationships
 *
 * SocialController handles:
 * - Trading with merchant NPCs (buy/sell items)
 * - Gift giving to NPCs (improves relationship)
 * - Relationship tracking via NPC memory system
 * - Price modifiers based on relationship level
 *
 * Uses the NPC memory system (NPCMemoryData) to track:
 * - MemoryType::Interaction for trades and gifts
 * - EmotionalState for relationship sentiment
 * - Memory value field for interaction quality (+/-)
 *
 * Ownership: ControllerRegistry owns the controller instance.
 */

#include "controllers/ControllerBase.hpp"
#include "entities/EntityHandle.hpp"
#include "managers/EntityDataManager.hpp"  // For INVALID_INVENTORY_INDEX
#include "utils/ResourceHandle.hpp"
#include <memory>
#include <string>

// Forward declarations
class Player;

/**
 * @brief Result of a trade operation
 */
enum class TradeResult {
    Success,              // Trade completed successfully
    InsufficientFunds,    // Buyer doesn't have enough gold/currency
    InsufficientStock,    // Seller doesn't have the item
    InvalidNPC,           // NPC handle invalid or not a merchant
    InvalidItem,          // Item handle invalid
    InventoryFull,        // Buyer's inventory is full
    NPCRefused            // NPC refused trade (relationship too low)
};

/**
 * @brief Type of social interaction for memory recording
 */
enum class InteractionType {
    Trade,        // Bought or sold items
    Gift,         // Gave item to NPC
    Greeting,     // Basic social interaction
    Help,         // Helped the NPC (quest, rescue)
    Theft,        // Stole from NPC (negative)
    Insult        // Negative social interaction
};

class SocialController : public ControllerBase {
public:
    /**
     * @brief Construct SocialController with required player reference
     * @param player Shared pointer to the player (required)
     */
    explicit SocialController(std::shared_ptr<Player> player)
        : mp_player(std::move(player)) {}

    ~SocialController() override = default;

    // Movable
    SocialController(SocialController&&) noexcept = default;
    SocialController& operator=(SocialController&&) noexcept = default;

    // --- ControllerBase interface ---

    void subscribe() override;

    [[nodiscard]] std::string_view getName() const override { return "SocialController"; }

    // ========================================================================
    // TRADING
    // ========================================================================

    /**
     * @brief Attempt to buy an item from an NPC merchant
     * @param npcHandle NPC to buy from
     * @param itemHandle Resource type to buy
     * @param quantity Number of items to buy
     * @return TradeResult indicating success or failure reason
     *
     * Price is calculated as: baseValue * getPriceModifier(npcHandle) * BUY_PRICE_MULTIPLIER
     * Successful trades improve relationship with the NPC.
     */
    TradeResult tryBuy(EntityHandle npcHandle,
                       HammerEngine::ResourceHandle itemHandle,
                       int quantity = 1);

    /**
     * @brief Attempt to sell an item to an NPC merchant
     * @param npcHandle NPC to sell to
     * @param itemHandle Resource type to sell
     * @param quantity Number of items to sell
     * @return TradeResult indicating success or failure reason
     *
     * Price is calculated as: baseValue * getPriceModifier(npcHandle) * SELL_PRICE_MULTIPLIER
     * Successful trades improve relationship with the NPC.
     */
    TradeResult trySell(EntityHandle npcHandle,
                        HammerEngine::ResourceHandle itemHandle,
                        int quantity = 1);

    /**
     * @brief Calculate the buy price for an item from a specific NPC
     * @param npcHandle NPC merchant
     * @param itemHandle Resource type
     * @param quantity Number of items
     * @return Total price in gold/currency units
     */
    [[nodiscard]] float calculateBuyPrice(EntityHandle npcHandle,
                                          HammerEngine::ResourceHandle itemHandle,
                                          int quantity = 1) const;

    /**
     * @brief Calculate the sell price for an item to a specific NPC
     * @param npcHandle NPC merchant
     * @param itemHandle Resource type
     * @param quantity Number of items
     * @return Total price in gold/currency units
     */
    [[nodiscard]] float calculateSellPrice(EntityHandle npcHandle,
                                           HammerEngine::ResourceHandle itemHandle,
                                           int quantity = 1) const;

    // ========================================================================
    // GIFTS & INTERACTIONS
    // ========================================================================

    /**
     * @brief Give an item to an NPC as a gift
     * @param npcHandle NPC to give gift to
     * @param itemHandle Resource type to give
     * @param quantity Number of items to give
     * @return true if gift was given successfully
     *
     * Gifts significantly improve relationship based on item value.
     * NPCs remember gifts and become more friendly.
     */
    bool tryGift(EntityHandle npcHandle,
                 HammerEngine::ResourceHandle itemHandle,
                 int quantity = 1);

    /**
     * @brief Record a generic social interaction
     * @param npcHandle NPC interacted with
     * @param type Type of interaction
     * @param value Interaction quality (-1.0 to +1.0, or item value for trades)
     *
     * Use this for non-trade interactions like greetings, help, or negative events.
     */
    void recordInteraction(EntityHandle npcHandle,
                           InteractionType type,
                           float value = 0.0f);

    /**
     * @brief Report a theft to the system
     * @param thief EntityHandle of the thief (typically player)
     * @param victim EntityHandle of the NPC who was robbed
     * @param stolenItem ResourceHandle of what was stolen
     * @param quantity Number of items stolen
     *
     * This will:
     * - Record the theft in the victim's memory (severe relationship damage)
     * - Fire a TheftEvent that nearby guards can respond to
     * - Alert nearby guards
     */
    void reportTheft(EntityHandle thief,
                     EntityHandle victim,
                     HammerEngine::ResourceHandle stolenItem,
                     int quantity = 1);

    /**
     * @brief Alert nearby guards to a crime at a location
     * @param location World position where the crime occurred
     * @param criminal EntityHandle of the criminal (target for guards)
     *
     * Guards within GUARD_ALERT_RANGE will be alerted and respond to the threat.
     */
    void alertNearbyGuards(const Vector2D& location, EntityHandle criminal);

    // ========================================================================
    // RELATIONSHIP
    // ========================================================================

    /**
     * @brief Get relationship level with an NPC
     * @param npcHandle NPC to check
     * @return Relationship score from -1.0 (hostile) to +1.0 (best friend)
     *
     * Calculated from NPC's memory of interactions and emotional state.
     * New NPCs start at 0.0 (neutral).
     */
    [[nodiscard]] float getRelationshipLevel(EntityHandle npcHandle) const;

    /**
     * @brief Get price modifier based on relationship
     * @param npcHandle NPC merchant
     * @return Multiplier from 0.7 (best friend) to 1.3 (hostile)
     *
     * Better relationships mean better prices for both buying and selling.
     */
    [[nodiscard]] float getPriceModifier(EntityHandle npcHandle) const;

    /**
     * @brief Check if NPC will refuse to trade due to poor relationship
     * @param npcHandle NPC to check
     * @return true if relationship is too low for trading
     */
    [[nodiscard]] bool willRefuseTrade(EntityHandle npcHandle) const;

    /**
     * @brief Get a description of the relationship level
     * @param npcHandle NPC to check
     * @return String like "Friendly", "Neutral", "Hostile", etc.
     */
    [[nodiscard]] std::string getRelationshipDescription(EntityHandle npcHandle) const;

    // ========================================================================
    // NPC INVENTORY HELPERS
    // ========================================================================

    /**
     * @brief Check if an NPC has merchant capability (has inventory)
     * @param npcHandle NPC to check
     * @return true if NPC can trade
     */
    [[nodiscard]] bool isMerchant(EntityHandle npcHandle) const;

    /**
     * @brief Get NPC's inventory index for direct access
     * @param npcHandle NPC to query
     * @return Inventory index, or INVALID_INVENTORY_INDEX if not a merchant
     */
    [[nodiscard]] uint32_t getNPCInventoryIndex(EntityHandle npcHandle) const;

    // ========================================================================
    // CONFIGURATION
    // ========================================================================

    // Price multipliers (buy price > sell price for merchant profit)
    static constexpr float BUY_PRICE_MULTIPLIER = 1.2f;   // 20% markup when buying
    static constexpr float SELL_PRICE_MULTIPLIER = 0.6f;  // 40% markdown when selling

    // Relationship thresholds
    static constexpr float RELATIONSHIP_HOSTILE = -0.5f;   // Won't trade
    static constexpr float RELATIONSHIP_UNFRIENDLY = -0.25f;
    static constexpr float RELATIONSHIP_NEUTRAL = 0.0f;
    static constexpr float RELATIONSHIP_FRIENDLY = 0.25f;
    static constexpr float RELATIONSHIP_TRUSTED = 0.5f;    // Best prices

    // Relationship changes per interaction
    static constexpr float TRADE_RELATIONSHIP_GAIN = 0.02f;   // Per successful trade
    static constexpr float GIFT_RELATIONSHIP_BASE = 0.05f;    // Base gift bonus
    static constexpr float GIFT_VALUE_SCALE = 0.001f;         // Additional per gold value
    static constexpr float THEFT_RELATIONSHIP_LOSS = -0.3f;   // Per theft
    static constexpr float GUARD_ALERT_RANGE = 500.0f;        // Guards within range respond to theft

private:
    /**
     * @brief Record a trade in NPC's memory
     */
    void recordTrade(EntityHandle npcHandle, float tradeValue, bool wasGoodDeal);

    /**
     * @brief Record a gift in NPC's memory
     */
    void recordGift(EntityHandle npcHandle, float giftValue);

    /**
     * @brief Update NPC's emotional state based on interaction
     */
    void updateEmotions(EntityHandle npcHandle, InteractionType type, float value);

    /**
     * @brief Get base item value from ResourceTemplateManager
     */
    [[nodiscard]] float getItemBaseValue(HammerEngine::ResourceHandle itemHandle) const;

    // Player reference
    std::weak_ptr<Player> mp_player;
};

#endif // SOCIAL_CONTROLLER_HPP
