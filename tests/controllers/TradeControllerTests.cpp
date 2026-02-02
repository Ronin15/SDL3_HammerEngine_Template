/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/**
 * @file TradeControllerTests.cpp
 * @brief Tests for TradeController
 *
 * Tests trade UI management between player and NPC merchants.
 */

#define BOOST_TEST_MODULE TradeControllerTests
#include <boost/test/unit_test.hpp>

#include "controllers/social/TradeController.hpp"
#include "controllers/social/SocialController.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/GameTimeManager.hpp"
#include "managers/UIManager.hpp"
#include "../events/EventManagerTestAccess.hpp"
#include <memory>

// ============================================================================
// Test Fixture
// ============================================================================

class TradeControllerTestFixture {
public:
    TradeControllerTestFixture() {
        // Reset EventManager to clean state
        EventManagerTestAccess::reset();
        EventManager::Instance().init();

        // Initialize managers
        EntityDataManager::Instance().init();
        GameTimeManager::Instance().init();
        UIManager::Instance().init();
    }

    ~TradeControllerTestFixture() {
        UIManager::Instance().clean();
        EntityDataManager::Instance().clean();
        EventManager::Instance().clean();
    }

    // Non-copyable
    TradeControllerTestFixture(const TradeControllerTestFixture&) = delete;
    TradeControllerTestFixture& operator=(const TradeControllerTestFixture&) = delete;
};

// ============================================================================
// TradeItemInfo Struct Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(TradeItemInfoTests)

BOOST_AUTO_TEST_CASE(TestDefaultValues) {
    TradeItemInfo info;

    BOOST_CHECK(!info.handle.isValid());
    BOOST_CHECK(info.name.empty());
    BOOST_CHECK_EQUAL(info.quantity, 0);
    BOOST_CHECK_EQUAL(info.unitPrice, 0.0f);
}

BOOST_AUTO_TEST_CASE(TestSetValues) {
    TradeItemInfo info;
    info.handle = HammerEngine::ResourceHandle(1, 1);
    info.name = "Test Item";
    info.quantity = 10;
    info.unitPrice = 5.0f;

    BOOST_CHECK(info.handle.isValid());
    BOOST_CHECK_EQUAL(info.name, "Test Item");
    BOOST_CHECK_EQUAL(info.quantity, 10);
    BOOST_CHECK_EQUAL(info.unitPrice, 5.0f);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Basic State Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(TradeControllerStateTests, TradeControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestTradeControllerName) {
    TradeController controller(nullptr);

    BOOST_CHECK_EQUAL(controller.getName(), "TradeController");
}

BOOST_AUTO_TEST_CASE(TestInitialState) {
    TradeController controller(nullptr);

    BOOST_CHECK(!controller.isTrading());
    BOOST_CHECK(!controller.getMerchantHandle().isValid());
    BOOST_CHECK(controller.getMerchantItems().empty());
    BOOST_CHECK(controller.getPlayerItems().empty());
    BOOST_CHECK_EQUAL(controller.getQuantity(), 1);
    BOOST_CHECK_EQUAL(controller.getSelectedMerchantIndex(), -1);
    BOOST_CHECK_EQUAL(controller.getSelectedPlayerIndex(), -1);
}

BOOST_AUTO_TEST_CASE(TestSubscribe) {
    TradeController controller(nullptr);

    controller.subscribe();
    BOOST_CHECK(controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestDoubleSubscribe) {
    TradeController controller(nullptr);

    controller.subscribe();
    controller.subscribe();  // Should be no-op
    BOOST_CHECK(controller.isSubscribed());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Trade Session Tests (No Player)
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(TradeControllerSessionTests, TradeControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestOpenTradeWithInvalidNPC) {
    TradeController controller(nullptr);

    EntityHandle invalidHandle;

    bool result = controller.openTrade(invalidHandle);
    BOOST_CHECK(!result);
    BOOST_CHECK(!controller.isTrading());
}

BOOST_AUTO_TEST_CASE(TestCloseTradeWhenNotTrading) {
    TradeController controller(nullptr);

    // Should not crash when closing without active trade
    controller.closeTrade();
    BOOST_CHECK(!controller.isTrading());
}

BOOST_AUTO_TEST_CASE(TestUpdateWhenNotTrading) {
    TradeController controller(nullptr);

    // Should not crash
    controller.update(0.016f);
    BOOST_CHECK(!controller.isTrading());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Selection Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(TradeControllerSelectionTests, TradeControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestSelectMerchantItemOutOfBounds) {
    TradeController controller(nullptr);

    // No items in list, should not crash
    controller.selectMerchantItem(0);
    controller.selectMerchantItem(100);

    BOOST_CHECK_EQUAL(controller.getSelectedMerchantIndex(), -1);
}

BOOST_AUTO_TEST_CASE(TestSelectPlayerItemOutOfBounds) {
    TradeController controller(nullptr);

    // No items in list, should not crash
    controller.selectPlayerItem(0);
    controller.selectPlayerItem(100);

    BOOST_CHECK_EQUAL(controller.getSelectedPlayerIndex(), -1);
}

BOOST_AUTO_TEST_CASE(TestSetQuantityZero) {
    TradeController controller(nullptr);

    controller.setQuantity(0);
    // Quantity should be clamped to minimum of 1
    BOOST_CHECK_EQUAL(controller.getQuantity(), 1);
}

BOOST_AUTO_TEST_CASE(TestSetQuantityNegative) {
    TradeController controller(nullptr);

    controller.setQuantity(-5);
    // Quantity should be clamped to minimum of 1
    BOOST_CHECK_EQUAL(controller.getQuantity(), 1);
}

BOOST_AUTO_TEST_CASE(TestSetQuantityPositive) {
    TradeController controller(nullptr);

    controller.setQuantity(10);
    // With no selection, quantity is not clamped to item quantity
    BOOST_CHECK_EQUAL(controller.getQuantity(), 10);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Transaction Tests (No Trading)
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(TradeControllerTransactionTests, TradeControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestExecuteBuyWhenNotTrading) {
    TradeController controller(nullptr);

    TradeResult result = controller.executeBuy();
    BOOST_CHECK(result == TradeResult::InvalidItem);
}

BOOST_AUTO_TEST_CASE(TestExecuteSellWhenNotTrading) {
    TradeController controller(nullptr);

    TradeResult result = controller.executeSell();
    BOOST_CHECK(result == TradeResult::InvalidItem);
}

BOOST_AUTO_TEST_CASE(TestGetCurrentBuyPriceNoSelection) {
    TradeController controller(nullptr);

    float price = controller.getCurrentBuyPrice();
    BOOST_CHECK_EQUAL(price, 0.0f);
}

BOOST_AUTO_TEST_CASE(TestGetCurrentSellPriceNoSelection) {
    TradeController controller(nullptr);

    float price = controller.getCurrentSellPrice();
    BOOST_CHECK_EQUAL(price, 0.0f);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Accessor Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(TradeControllerAccessorTests, TradeControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestGetRelationshipDescriptionWhenNotTrading) {
    TradeController controller(nullptr);

    std::string desc = controller.getRelationshipDescription();
    BOOST_CHECK_EQUAL(desc, "N/A");
}

BOOST_AUTO_TEST_CASE(TestGetPriceModifierWhenNotTrading) {
    TradeController controller(nullptr);

    float modifier = controller.getPriceModifier();
    BOOST_CHECK_EQUAL(modifier, 1.0f);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// With SocialController Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(TradeControllerWithSocialTests, TradeControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestConstructWithSocialController) {
    SocialController social(nullptr);
    TradeController trade(nullptr, &social);

    BOOST_CHECK_EQUAL(trade.getName(), "TradeController");
    BOOST_CHECK(!trade.isTrading());
}

BOOST_AUTO_TEST_CASE(TestExecuteBuyWithSocialControllerNoTrade) {
    SocialController social(nullptr);
    TradeController trade(nullptr, &social);

    // Should still fail - not trading
    TradeResult result = trade.executeBuy();
    BOOST_CHECK(result == TradeResult::InvalidItem);
}

BOOST_AUTO_TEST_CASE(TestExecuteSellWithSocialControllerNoTrade) {
    SocialController social(nullptr);
    TradeController trade(nullptr, &social);

    // Should still fail - not trading
    TradeResult result = trade.executeSell();
    BOOST_CHECK(result == TradeResult::InvalidItem);
}

BOOST_AUTO_TEST_SUITE_END()
