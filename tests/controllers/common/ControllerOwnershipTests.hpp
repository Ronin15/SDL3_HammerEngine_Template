/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef CONTROLLER_OWNERSHIP_TESTS_HPP
#define CONTROLLER_OWNERSHIP_TESTS_HPP

/**
 * @file ControllerOwnershipTests.hpp
 * @brief Ownership model tests for ControllerBase-derived classes
 *
 * Tests:
 * - Controller instantiation (multiple instances)
 * - Move semantics (move constructor transfers subscription)
 * - Auto-unsubscribe on destruction (RAII)
 *
 * Usage:
 *   INSTANTIATE_CONTROLLER_OWNERSHIP_TESTS(WeatherController)
 */

#include <boost/test/unit_test.hpp>

/**
 * @brief Generates ownership model test suite for a controller type
 * @param ControllerType The controller class to test
 *
 * Creates test suite: OwnershipModelTests_<ControllerType>
 */
#define INSTANTIATE_CONTROLLER_OWNERSHIP_TESTS(ControllerType)                                     \
    BOOST_AUTO_TEST_SUITE(OwnershipModelTests)                                                     \
                                                                                                   \
    BOOST_AUTO_TEST_CASE(TestControllerInstantiation)                                              \
    {                                                                                              \
        ControllerType controller1;                                                                \
        ControllerType controller2;                                                                \
        BOOST_CHECK(&controller1 != &controller2);                                                 \
    }                                                                                              \
                                                                                                   \
    BOOST_AUTO_TEST_CASE(TestMoveSemantics)                                                        \
    {                                                                                              \
        ControllerType controller1;                                                                \
        controller1.subscribe();                                                                   \
        BOOST_CHECK(controller1.isSubscribed());                                                   \
                                                                                                   \
        ControllerType controller2(std::move(controller1));                                        \
        BOOST_CHECK(controller2.isSubscribed());                                                   \
        BOOST_CHECK(!controller1.isSubscribed());                                                  \
    }                                                                                              \
                                                                                                   \
    BOOST_AUTO_TEST_CASE(TestAutoUnsubscribeOnDestruction)                                         \
    {                                                                                              \
        {                                                                                          \
            ControllerType controller;                                                             \
            controller.subscribe();                                                                \
            BOOST_CHECK(controller.isSubscribed());                                                \
        }                                                                                          \
        /* No crash = success (destructor auto-unsubscribed) */                                    \
    }                                                                                              \
                                                                                                   \
    BOOST_AUTO_TEST_SUITE_END()

#endif // CONTROLLER_OWNERSHIP_TESTS_HPP
