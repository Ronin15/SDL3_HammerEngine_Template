/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef CONTROLLER_GET_NAME_TESTS_HPP
#define CONTROLLER_GET_NAME_TESTS_HPP

/**
 * @file ControllerGetNameTests.hpp
 * @brief getName() tests for ControllerBase-derived classes
 *
 * Tests:
 * - getName returns correct controller name
 * - getName returns valid string_view (not empty)
 *
 * Usage:
 *   using MyFixture = ControllerTestFixture<MyController>;
 *   INSTANTIATE_CONTROLLER_GET_NAME_TESTS(MyController, MyFixture, "MyController")
 */

#include <boost/test/unit_test.hpp>
#include <string_view>

/**
 * @brief Generates getName test suite for a controller type
 * @param ControllerType The controller class to test
 * @param FixtureType The test fixture class (usually ControllerTestFixture<ControllerType>)
 * @param ExpectedName The expected controller name string (e.g., "WeatherController")
 *
 * Creates test suite: GetNameTests
 */
#define INSTANTIATE_CONTROLLER_GET_NAME_TESTS(ControllerType, FixtureType, ExpectedName)           \
    BOOST_AUTO_TEST_SUITE(GetNameTests)                                                            \
                                                                                                   \
    BOOST_FIXTURE_TEST_CASE(TestGetNameReturnsCorrectName, FixtureType)                            \
    {                                                                                              \
        BOOST_CHECK_EQUAL(m_controller.getName(), ExpectedName);                                   \
    }                                                                                              \
                                                                                                   \
    BOOST_FIXTURE_TEST_CASE(TestGetNameReturnsStringView, FixtureType)                             \
    {                                                                                              \
        std::string_view name = m_controller.getName();                                            \
        BOOST_CHECK(!name.empty());                                                                \
        BOOST_CHECK_EQUAL(name, ExpectedName);                                                     \
    }                                                                                              \
                                                                                                   \
    BOOST_AUTO_TEST_SUITE_END()

#endif // CONTROLLER_GET_NAME_TESTS_HPP
