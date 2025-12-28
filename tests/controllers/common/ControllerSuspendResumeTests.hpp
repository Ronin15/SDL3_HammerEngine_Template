/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef CONTROLLER_SUSPEND_RESUME_TESTS_HPP
#define CONTROLLER_SUSPEND_RESUME_TESTS_HPP

/**
 * @file ControllerSuspendResumeTests.hpp
 * @brief Suspend/resume lifecycle tests for ControllerBase-derived classes
 *
 * Tests:
 * - Initially not suspended
 * - Suspend sets flag and unsubscribes
 * - Resume clears flag and re-subscribes
 * - Suspend/resume are idempotent
 * - Suspend without prior subscribe is safe
 * - Resume without prior suspend is safe
 *
 * Usage:
 *   using MyFixture = ControllerTestFixture<MyController>;
 *   INSTANTIATE_CONTROLLER_SUSPEND_RESUME_TESTS(MyController, MyFixture)
 */

#include <boost/test/unit_test.hpp>

/**
 * @brief Generates suspend/resume test suite for a controller type
 * @param ControllerType The controller class to test
 * @param FixtureType The test fixture class (usually ControllerTestFixture<ControllerType>)
 *
 * Creates test suite: SuspendResumeTests
 */
#define INSTANTIATE_CONTROLLER_SUSPEND_RESUME_TESTS(ControllerType, FixtureType)                   \
    BOOST_AUTO_TEST_SUITE(SuspendResumeTests)                                                      \
                                                                                                   \
    BOOST_FIXTURE_TEST_CASE(TestInitiallyNotSuspended, FixtureType)                                \
    {                                                                                              \
        BOOST_CHECK(!m_controller.isSuspended());                                                  \
    }                                                                                              \
                                                                                                   \
    BOOST_FIXTURE_TEST_CASE(TestSuspendSetsFlag, FixtureType)                                      \
    {                                                                                              \
        m_controller.subscribe();                                                                  \
        BOOST_CHECK(!m_controller.isSuspended());                                                  \
                                                                                                   \
        m_controller.suspend();                                                                    \
                                                                                                   \
        BOOST_CHECK(m_controller.isSuspended());                                                   \
        BOOST_CHECK(!m_controller.isSubscribed()); /* Default suspend unsubscribes */              \
    }                                                                                              \
                                                                                                   \
    BOOST_FIXTURE_TEST_CASE(TestResumeClearsFlag, FixtureType)                                     \
    {                                                                                              \
        m_controller.subscribe();                                                                  \
        m_controller.suspend();                                                                    \
        BOOST_CHECK(m_controller.isSuspended());                                                   \
                                                                                                   \
        m_controller.resume();                                                                     \
                                                                                                   \
        BOOST_CHECK(!m_controller.isSuspended());                                                  \
        BOOST_CHECK(m_controller.isSubscribed()); /* Default resume re-subscribes */               \
    }                                                                                              \
                                                                                                   \
    BOOST_FIXTURE_TEST_CASE(TestSuspendResumeIdempotent, FixtureType)                              \
    {                                                                                              \
        m_controller.subscribe();                                                                  \
                                                                                                   \
        /* Double suspend should be safe */                                                        \
        m_controller.suspend();                                                                    \
        m_controller.suspend();                                                                    \
        BOOST_CHECK(m_controller.isSuspended());                                                   \
                                                                                                   \
        /* Double resume should be safe */                                                         \
        m_controller.resume();                                                                     \
        m_controller.resume();                                                                     \
        BOOST_CHECK(!m_controller.isSuspended());                                                  \
        BOOST_CHECK(m_controller.isSubscribed());                                                  \
    }                                                                                              \
                                                                                                   \
    BOOST_FIXTURE_TEST_CASE(TestSuspendWithoutSubscribe, FixtureType)                              \
    {                                                                                              \
        /* Suspend without having subscribed first should be safe */                               \
        m_controller.suspend();                                                                    \
        BOOST_CHECK(m_controller.isSuspended());                                                   \
        BOOST_CHECK(!m_controller.isSubscribed());                                                 \
    }                                                                                              \
                                                                                                   \
    BOOST_FIXTURE_TEST_CASE(TestResumeWithoutSuspend, FixtureType)                                 \
    {                                                                                              \
        /* Resume without having suspended first should be safe */                                 \
        m_controller.subscribe();                                                                  \
        m_controller.resume(); /* No-op when not suspended */                                      \
        BOOST_CHECK(!m_controller.isSuspended());                                                  \
        BOOST_CHECK(m_controller.isSubscribed());                                                  \
    }                                                                                              \
                                                                                                   \
    BOOST_AUTO_TEST_SUITE_END()

#endif // CONTROLLER_SUSPEND_RESUME_TESTS_HPP
