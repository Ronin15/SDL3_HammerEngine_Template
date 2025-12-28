/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef CONTROLLER_SUBSCRIPTION_TESTS_HPP
#define CONTROLLER_SUBSCRIPTION_TESTS_HPP

/**
 * @file ControllerSubscriptionTests.hpp
 * @brief Subscription lifecycle tests for ControllerBase-derived classes
 *
 * Tests:
 * - Initially not subscribed
 * - Subscribe/unsubscribe state changes
 * - Subscribe/unsubscribe cycles
 * - Idempotent subscribe (double subscribe ignored)
 * - Idempotent unsubscribe (double unsubscribe safe)
 *
 * Usage:
 *   using MyFixture = ControllerTestFixture<MyController>;
 *   INSTANTIATE_CONTROLLER_SUBSCRIPTION_TESTS(MyController, MyFixture)
 */

#include <boost/test/unit_test.hpp>

/**
 * @brief Generates subscription test suite for a controller type
 * @param ControllerType The controller class to test
 * @param FixtureType The test fixture class (usually ControllerTestFixture<ControllerType>)
 *
 * Creates test suite: SubscriptionTests
 */
#define INSTANTIATE_CONTROLLER_SUBSCRIPTION_TESTS(ControllerType, FixtureType)                     \
    BOOST_AUTO_TEST_SUITE(SubscriptionTests)                                                       \
                                                                                                   \
    BOOST_FIXTURE_TEST_CASE(TestInitiallyNotSubscribed, FixtureType)                               \
    {                                                                                              \
        BOOST_CHECK(!m_controller.isSubscribed());                                                 \
    }                                                                                              \
                                                                                                   \
    BOOST_FIXTURE_TEST_CASE(TestSubscribe, FixtureType)                                            \
    {                                                                                              \
        m_controller.subscribe();                                                                  \
        BOOST_CHECK(m_controller.isSubscribed());                                                  \
    }                                                                                              \
                                                                                                   \
    BOOST_FIXTURE_TEST_CASE(TestUnsubscribe, FixtureType)                                          \
    {                                                                                              \
        m_controller.subscribe();                                                                  \
        BOOST_CHECK(m_controller.isSubscribed());                                                  \
                                                                                                   \
        m_controller.unsubscribe();                                                                \
        BOOST_CHECK(!m_controller.isSubscribed());                                                 \
    }                                                                                              \
                                                                                                   \
    BOOST_FIXTURE_TEST_CASE(TestSubscribeUnsubscribeCycle, FixtureType)                            \
    {                                                                                              \
        for (int i = 0; i < 3; ++i) {                                                              \
            m_controller.subscribe();                                                              \
            BOOST_CHECK(m_controller.isSubscribed());                                              \
                                                                                                   \
            m_controller.unsubscribe();                                                            \
            BOOST_CHECK(!m_controller.isSubscribed());                                             \
        }                                                                                          \
    }                                                                                              \
                                                                                                   \
    BOOST_FIXTURE_TEST_CASE(TestDoubleSubscribeIgnored, FixtureType)                               \
    {                                                                                              \
        m_controller.subscribe();                                                                  \
        BOOST_CHECK(m_controller.isSubscribed());                                                  \
                                                                                                   \
        m_controller.subscribe(); /* Second subscribe should be ignored */                         \
        BOOST_CHECK(m_controller.isSubscribed());                                                  \
                                                                                                   \
        m_controller.unsubscribe(); /* Single unsubscribe should fully unsubscribe */              \
        BOOST_CHECK(!m_controller.isSubscribed());                                                 \
    }                                                                                              \
                                                                                                   \
    BOOST_FIXTURE_TEST_CASE(TestDoubleUnsubscribeIgnored, FixtureType)                             \
    {                                                                                              \
        m_controller.subscribe();                                                                  \
        BOOST_CHECK(m_controller.isSubscribed());                                                  \
                                                                                                   \
        m_controller.unsubscribe();                                                                \
        BOOST_CHECK(!m_controller.isSubscribed());                                                 \
                                                                                                   \
        m_controller.unsubscribe(); /* Second unsubscribe should be safe */                        \
        BOOST_CHECK(!m_controller.isSubscribed());                                                 \
    }                                                                                              \
                                                                                                   \
    BOOST_AUTO_TEST_SUITE_END()

#endif // CONTROLLER_SUBSCRIPTION_TESTS_HPP
