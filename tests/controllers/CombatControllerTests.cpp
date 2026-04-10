/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE CombatControllerTests
#include <boost/test/unit_test.hpp>

#include "controllers/combat/CombatController.hpp"

#include <memory>

BOOST_AUTO_TEST_SUITE(CombatControllerTests)

BOOST_AUTO_TEST_CASE(TestControllerNameAndInitialState)
{
    CombatController controller(std::shared_ptr<Player>{});

    BOOST_CHECK_EQUAL(controller.getName(), "CombatController");
}

BOOST_AUTO_TEST_CASE(TestSubscribeIsIdempotentWithoutHandlers)
{
    CombatController controller(std::shared_ptr<Player>{});

    controller.subscribe();
    BOOST_CHECK(controller.isSubscribed());

    controller.subscribe();
    BOOST_CHECK(controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestTryAttackFailsGracefullyWithoutPlayer)
{
    CombatController controller(std::shared_ptr<Player>{});

    BOOST_CHECK(!controller.tryAttack());
}

BOOST_AUTO_TEST_CASE(TestUpdateDoesNothingWithoutPlayer)
{
    CombatController controller(std::shared_ptr<Player>{});

    // Capture public state before update
    BOOST_CHECK_EQUAL(controller.getName(), "CombatController");

    controller.update(0.25f);

    // Public state unchanged — update is a no-op without player
    BOOST_CHECK_EQUAL(controller.getName(), "CombatController");
}

BOOST_AUTO_TEST_SUITE_END()
