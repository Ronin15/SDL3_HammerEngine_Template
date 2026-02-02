/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/**
 * @file SocialControllerTests.cpp
 * @brief Tests for SocialController
 *
 * Tests NPC social interactions: trading, gifts, and relationships.
 */

#define BOOST_TEST_MODULE SocialControllerTests
#include <boost/test/unit_test.hpp>

#include "controllers/social/SocialController.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/GameTimeManager.hpp"
#include "../events/EventManagerTestAccess.hpp"
#include <memory>

// ============================================================================
// Test Fixture
// ============================================================================

class SocialControllerTestFixture {
public:
    SocialControllerTestFixture() {
        // Reset EventManager to clean state
        EventManagerTestAccess::reset();
        EventManager::Instance().init();

        // Initialize managers
        EntityDataManager::Instance().init();
        GameTimeManager::Instance().init();
    }

    ~SocialControllerTestFixture() {
        EntityDataManager::Instance().clean();
        EventManager::Instance().clean();
    }

    // Non-copyable
    SocialControllerTestFixture(const SocialControllerTestFixture&) = delete;
    SocialControllerTestFixture& operator=(const SocialControllerTestFixture&) = delete;
};

// ============================================================================
// TradeResult Enum Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(TradeResultTests)

BOOST_AUTO_TEST_CASE(TestTradeResultValues) {
    // Ensure all enum values are distinct
    BOOST_CHECK(TradeResult::Success != TradeResult::InsufficientFunds);
    BOOST_CHECK(TradeResult::InsufficientFunds != TradeResult::InsufficientStock);
    BOOST_CHECK(TradeResult::InsufficientStock != TradeResult::InvalidNPC);
    BOOST_CHECK(TradeResult::InvalidNPC != TradeResult::InvalidItem);
    BOOST_CHECK(TradeResult::InvalidItem != TradeResult::InventoryFull);
    BOOST_CHECK(TradeResult::InventoryFull != TradeResult::NPCRefused);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// InteractionType Enum Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(InteractionTypeTests)

BOOST_AUTO_TEST_CASE(TestInteractionTypeValues) {
    // Ensure all enum values are distinct
    BOOST_CHECK(InteractionType::Trade != InteractionType::Gift);
    BOOST_CHECK(InteractionType::Gift != InteractionType::Greeting);
    BOOST_CHECK(InteractionType::Greeting != InteractionType::Help);
    BOOST_CHECK(InteractionType::Help != InteractionType::Theft);
    BOOST_CHECK(InteractionType::Theft != InteractionType::Insult);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Basic State Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(SocialControllerStateTests, SocialControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestSocialControllerName) {
    SocialController controller(nullptr);

    BOOST_CHECK_EQUAL(controller.getName(), "SocialController");
}

BOOST_AUTO_TEST_CASE(TestSubscribe) {
    SocialController controller(nullptr);

    controller.subscribe();
    BOOST_CHECK(controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestDoubleSubscribe) {
    SocialController controller(nullptr);

    controller.subscribe();
    controller.subscribe();  // Should be no-op
    BOOST_CHECK(controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestConstants) {
    // Verify price multipliers are reasonable
    BOOST_CHECK_GT(SocialController::BUY_PRICE_MULTIPLIER, 1.0f);  // Markup
    BOOST_CHECK_LT(SocialController::SELL_PRICE_MULTIPLIER, 1.0f); // Markdown

    // Verify relationship thresholds are ordered
    BOOST_CHECK_LT(SocialController::RELATIONSHIP_HOSTILE, SocialController::RELATIONSHIP_UNFRIENDLY);
    BOOST_CHECK_LT(SocialController::RELATIONSHIP_UNFRIENDLY, SocialController::RELATIONSHIP_NEUTRAL);
    BOOST_CHECK_LT(SocialController::RELATIONSHIP_NEUTRAL, SocialController::RELATIONSHIP_FRIENDLY);
    BOOST_CHECK_LT(SocialController::RELATIONSHIP_FRIENDLY, SocialController::RELATIONSHIP_TRUSTED);

    // Verify guard alert range is positive
    BOOST_CHECK_GT(SocialController::GUARD_ALERT_RANGE, 0.0f);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Trading Tests (No Player)
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(SocialControllerTradingTests, SocialControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestTryBuyWithInvalidNPC) {
    SocialController controller(nullptr);

    EntityHandle invalidHandle;
    HammerEngine::ResourceHandle itemHandle(1, 1);

    TradeResult result = controller.tryBuy(invalidHandle, itemHandle, 1);
    BOOST_CHECK(result == TradeResult::InvalidNPC);
}

BOOST_AUTO_TEST_CASE(TestTrySellWithInvalidNPC) {
    SocialController controller(nullptr);

    EntityHandle invalidHandle;
    HammerEngine::ResourceHandle itemHandle(1, 1);

    TradeResult result = controller.trySell(invalidHandle, itemHandle, 1);
    BOOST_CHECK(result == TradeResult::InvalidNPC);
}

BOOST_AUTO_TEST_CASE(TestTryGiftWithInvalidNPC) {
    SocialController controller(nullptr);

    EntityHandle invalidHandle;
    HammerEngine::ResourceHandle itemHandle(1, 1);

    bool result = controller.tryGift(invalidHandle, itemHandle, 1);
    BOOST_CHECK(!result);
}

BOOST_AUTO_TEST_CASE(TestCalculateBuyPriceWithInvalidItem) {
    SocialController controller(nullptr);

    EntityHandle npcHandle;
    HammerEngine::ResourceHandle invalidItem;

    float price = controller.calculateBuyPrice(npcHandle, invalidItem, 1);
    BOOST_CHECK_EQUAL(price, 0.0f);
}

BOOST_AUTO_TEST_CASE(TestCalculateSellPriceWithInvalidItem) {
    SocialController controller(nullptr);

    EntityHandle npcHandle;
    HammerEngine::ResourceHandle invalidItem;

    float price = controller.calculateSellPrice(npcHandle, invalidItem, 1);
    BOOST_CHECK_EQUAL(price, 0.0f);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Relationship Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(SocialControllerRelationshipTests, SocialControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestGetRelationshipLevelWithInvalidNPC) {
    SocialController controller(nullptr);

    EntityHandle invalidHandle;

    float level = controller.getRelationshipLevel(invalidHandle);
    BOOST_CHECK_EQUAL(level, SocialController::RELATIONSHIP_NEUTRAL);
}

BOOST_AUTO_TEST_CASE(TestGetPriceModifierWithInvalidNPC) {
    SocialController controller(nullptr);

    EntityHandle invalidHandle;

    // Neutral relationship should give 1.0 price modifier
    float modifier = controller.getPriceModifier(invalidHandle);
    BOOST_CHECK_CLOSE(modifier, 1.0f, 0.01f);
}

BOOST_AUTO_TEST_CASE(TestWillRefuseTradeWithInvalidNPC) {
    SocialController controller(nullptr);

    EntityHandle invalidHandle;

    // Invalid NPC with neutral relationship should not refuse
    bool willRefuse = controller.willRefuseTrade(invalidHandle);
    BOOST_CHECK(!willRefuse);
}

BOOST_AUTO_TEST_CASE(TestGetRelationshipDescriptionNeutral) {
    SocialController controller(nullptr);

    EntityHandle invalidHandle;

    std::string desc = controller.getRelationshipDescription(invalidHandle);
    BOOST_CHECK_EQUAL(desc, "Neutral");
}

BOOST_AUTO_TEST_CASE(TestIsMerchantWithInvalidNPC) {
    SocialController controller(nullptr);

    EntityHandle invalidHandle;

    bool isMerchant = controller.isMerchant(invalidHandle);
    BOOST_CHECK(!isMerchant);
}

BOOST_AUTO_TEST_CASE(TestGetNPCInventoryIndexWithInvalidNPC) {
    SocialController controller(nullptr);

    EntityHandle invalidHandle;

    uint32_t invIdx = controller.getNPCInventoryIndex(invalidHandle);
    BOOST_CHECK_EQUAL(invIdx, INVALID_INVENTORY_INDEX);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Interaction Recording Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(SocialControllerInteractionTests, SocialControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestRecordInteractionWithInvalidNPC) {
    SocialController controller(nullptr);

    EntityHandle invalidHandle;

    // Should not crash with invalid handle
    controller.recordInteraction(invalidHandle, InteractionType::Trade, 1.0f);
    controller.recordInteraction(invalidHandle, InteractionType::Gift, 1.0f);
    controller.recordInteraction(invalidHandle, InteractionType::Greeting, 1.0f);
    controller.recordInteraction(invalidHandle, InteractionType::Help, 1.0f);
    controller.recordInteraction(invalidHandle, InteractionType::Theft, -1.0f);
    controller.recordInteraction(invalidHandle, InteractionType::Insult, -1.0f);

    // Just verify no crash occurred
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestReportTheftWithInvalidVictim) {
    SocialController controller(nullptr);

    EntityHandle thief;
    EntityHandle victim;  // Invalid
    HammerEngine::ResourceHandle stolenItem(1, 1);

    // Should not crash with invalid handles
    controller.reportTheft(thief, victim, stolenItem, 1);

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
