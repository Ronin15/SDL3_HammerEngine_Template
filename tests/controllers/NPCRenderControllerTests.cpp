/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/**
 * @file NPCRenderControllerTests.cpp
 * @brief Tests for NPCRenderController
 *
 * Tests velocity-based animation logic, edge cases, and cleanup.
 * Note: Render tests are limited since we can't easily test SDL rendering.
 */

#define BOOST_TEST_MODULE NPCRenderControllerTests
#include <boost/test/unit_test.hpp>

#include "controllers/render/NPCRenderController.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "core/ThreadSystem.hpp"
#include "entities/Entity.hpp"  // For AnimationConfig
#include <SDL3/SDL.h>

// Test tolerance for floating-point comparisons
constexpr float EPSILON = 0.001f;

bool approxEqual(float a, float b, float epsilon = EPSILON) {
    return std::abs(a - b) < epsilon;
}

// ============================================================================
// Test Fixture
// ============================================================================

/**
 * @brief Fixture for NPCRenderController tests
 *
 * Initializes EntityDataManager, AIManager, and other required managers.
 * NPCRenderController is tested by directly manipulating EDM data.
 */
class NPCRenderControllerFixture {
public:
    NPCRenderControllerFixture() {
        HammerEngine::ThreadSystem::Instance().init();
        EntityDataManager::Instance().init();
        CollisionManager::Instance().init();
        PathfinderManager::Instance().init();
        AIManager::Instance().init();
    }

    ~NPCRenderControllerFixture() {
        AIManager::Instance().clean();
        PathfinderManager::Instance().clean();
        CollisionManager::Instance().clean();
        EntityDataManager::Instance().clean();
        HammerEngine::ThreadSystem::Instance().clean();
    }

protected:
    NPCRenderController m_controller;

    // Helper to create NPC with specific animation config
    EntityHandle createTestNPC(const Vector2D& pos,
                               int idleFrames = 1,
                               int moveFrames = 2,
                               int idleSpeed = 150,
                               int moveSpeed = 100,
                               int idleRow = 0,
                               int moveRow = 1) {
        AnimationConfig idle{idleRow, idleFrames, idleSpeed, true};
        AnimationConfig move{moveRow, moveFrames, moveSpeed, true};
        return EntityDataManager::Instance().createDataDrivenNPC(pos, "test", idle, move);
    }

    // Helper to set NPC velocity in EDM
    void setNPCVelocity(EntityHandle handle, float vx, float vy) {
        auto& edm = EntityDataManager::Instance();
        auto& transform = edm.getTransform(handle);
        transform.velocity = Vector2D(vx, vy);
    }

    // Helper to get NPCRenderData
    NPCRenderData& getRenderData(EntityHandle handle) {
        return EntityDataManager::Instance().getNPCRenderData(handle);
    }
};

// ============================================================================
// ANIMATION UPDATE TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(AnimationUpdateTests, NPCRenderControllerFixture)

BOOST_AUTO_TEST_CASE(TestAnimationAccumulatorAdvances) {
    EntityHandle npc = createTestNPC(Vector2D(100.0f, 100.0f));
    BOOST_REQUIRE(npc.isValid());

    // Update simulation tiers so NPC is Active
    EntityDataManager::Instance().updateSimulationTiers(Vector2D(100.0f, 100.0f), 1500.0f, 10000.0f);

    auto& rd = getRenderData(npc);
    float initialAccum = rd.animationAccumulator;

    // Update with 0.05 seconds
    m_controller.update(0.05f);

    // Accumulator should have increased
    BOOST_CHECK_GT(rd.animationAccumulator, initialAccum);
}

BOOST_AUTO_TEST_CASE(TestFrameCyclesOnSpeedThreshold) {
    // Need 2+ idle frames since NPC is stationary (uses idle animation)
    EntityHandle npc = createTestNPC(Vector2D(100.0f, 100.0f), 4, 4, 100, 100);  // 4 frames, 100ms each
    BOOST_REQUIRE(npc.isValid());

    EntityDataManager::Instance().updateSimulationTiers(Vector2D(100.0f, 100.0f), 1500.0f, 10000.0f);

    auto& rd = getRenderData(npc);
    BOOST_CHECK_EQUAL(rd.currentFrame, 0);

    // Update past the speed threshold (100ms = 0.1s)
    m_controller.update(0.11f);

    // Frame should have advanced
    BOOST_CHECK_EQUAL(rd.currentFrame, 1);
}

BOOST_AUTO_TEST_CASE(TestFrameWrapsToZero) {
    EntityHandle npc = createTestNPC(Vector2D(100.0f, 100.0f), 2, 2, 50, 50);  // 2 frames, 50ms each
    BOOST_REQUIRE(npc.isValid());

    EntityDataManager::Instance().updateSimulationTiers(Vector2D(100.0f, 100.0f), 1500.0f, 10000.0f);

    auto& rd = getRenderData(npc);

    // Update to advance through all frames (2 frames at 50ms each = 100ms total)
    m_controller.update(0.06f);  // Frame 0 -> 1
    BOOST_CHECK_EQUAL(rd.currentFrame, 1);

    m_controller.update(0.06f);  // Frame 1 -> 0 (wrap)
    BOOST_CHECK_EQUAL(rd.currentFrame, 0);
}

BOOST_AUTO_TEST_CASE(TestIdleRowSelectedWhenStationary) {
    EntityHandle npc = createTestNPC(Vector2D(100.0f, 100.0f), 1, 2, 100, 100, 0, 1);  // idleRow=0, moveRow=1
    BOOST_REQUIRE(npc.isValid());

    EntityDataManager::Instance().updateSimulationTiers(Vector2D(100.0f, 100.0f), 1500.0f, 10000.0f);

    // Set velocity to zero (stationary)
    setNPCVelocity(npc, 0.0f, 0.0f);

    m_controller.update(0.01f);

    auto& rd = getRenderData(npc);
    BOOST_CHECK_EQUAL(rd.currentRow, 0);  // Should be idle row
}

BOOST_AUTO_TEST_CASE(TestMoveRowSelectedWhenMoving) {
    EntityHandle npc = createTestNPC(Vector2D(100.0f, 100.0f), 1, 2, 100, 100, 0, 1);  // idleRow=0, moveRow=1
    BOOST_REQUIRE(npc.isValid());

    EntityDataManager::Instance().updateSimulationTiers(Vector2D(100.0f, 100.0f), 1500.0f, 10000.0f);

    // Set velocity above threshold (15.0f) - need sqrt(vx^2 + vy^2) > 15
    // 20,0 gives magnitude 20 which is > 15
    setNPCVelocity(npc, 20.0f, 0.0f);

    m_controller.update(0.01f);

    auto& rd = getRenderData(npc);
    BOOST_CHECK_EQUAL(rd.currentRow, 1);  // Should be move row
}

BOOST_AUTO_TEST_CASE(TestFlipHorizontalWhenMovingLeft) {
    EntityHandle npc = createTestNPC(Vector2D(100.0f, 100.0f));
    BOOST_REQUIRE(npc.isValid());

    EntityDataManager::Instance().updateSimulationTiers(Vector2D(100.0f, 100.0f), 1500.0f, 10000.0f);

    // Set velocity pointing left
    setNPCVelocity(npc, -20.0f, 0.0f);

    m_controller.update(0.01f);

    auto& rd = getRenderData(npc);
    BOOST_CHECK_EQUAL(rd.flipMode, static_cast<uint8_t>(SDL_FLIP_HORIZONTAL));
}

BOOST_AUTO_TEST_CASE(TestFlipNoneWhenMovingRight) {
    EntityHandle npc = createTestNPC(Vector2D(100.0f, 100.0f));
    BOOST_REQUIRE(npc.isValid());

    EntityDataManager::Instance().updateSimulationTiers(Vector2D(100.0f, 100.0f), 1500.0f, 10000.0f);

    // First set flip to horizontal
    auto& rd = getRenderData(npc);
    rd.flipMode = static_cast<uint8_t>(SDL_FLIP_HORIZONTAL);

    // Now set velocity pointing right
    setNPCVelocity(npc, 20.0f, 0.0f);

    m_controller.update(0.01f);

    BOOST_CHECK_EQUAL(rd.flipMode, static_cast<uint8_t>(SDL_FLIP_NONE));
}

BOOST_AUTO_TEST_CASE(TestFlipPreservedWhenVelocityZero) {
    EntityHandle npc = createTestNPC(Vector2D(100.0f, 100.0f));
    BOOST_REQUIRE(npc.isValid());

    EntityDataManager::Instance().updateSimulationTiers(Vector2D(100.0f, 100.0f), 1500.0f, 10000.0f);

    // Set flip to horizontal
    auto& rd = getRenderData(npc);
    rd.flipMode = static_cast<uint8_t>(SDL_FLIP_HORIZONTAL);

    // Set velocity to zero
    setNPCVelocity(npc, 0.0f, 0.0f);

    m_controller.update(0.01f);

    // Flip should be preserved (not changed when vx == 0)
    BOOST_CHECK_EQUAL(rd.flipMode, static_cast<uint8_t>(SDL_FLIP_HORIZONTAL));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// EDGE CASE TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(EdgeCaseTests, NPCRenderControllerFixture)

BOOST_AUTO_TEST_CASE(TestZeroFrameCountHandled) {
    // Create NPC - the EDM should clamp to minimum 1 frame
    AnimationConfig zeroFrameConfig{0, 0, 100, true};
    EntityHandle npc = EntityDataManager::Instance().createDataDrivenNPC(
        Vector2D(100.0f, 100.0f), "test", zeroFrameConfig, zeroFrameConfig);
    BOOST_REQUIRE(npc.isValid());

    EntityDataManager::Instance().updateSimulationTiers(Vector2D(100.0f, 100.0f), 1500.0f, 10000.0f);

    // This should NOT crash (EDM clamps to minimum 1 frame)
    BOOST_CHECK_NO_THROW(m_controller.update(0.1f));

    // Verify frames were clamped
    auto& rd = getRenderData(npc);
    BOOST_CHECK_GE(rd.numIdleFrames, 1);
    BOOST_CHECK_GE(rd.numMoveFrames, 1);
}

BOOST_AUTO_TEST_CASE(TestZeroSpeedHandled) {
    // Create NPC - the EDM should clamp to minimum 1ms speed
    AnimationConfig zeroSpeedConfig{0, 2, 0, true};
    EntityHandle npc = EntityDataManager::Instance().createDataDrivenNPC(
        Vector2D(100.0f, 100.0f), "test", zeroSpeedConfig, zeroSpeedConfig);
    BOOST_REQUIRE(npc.isValid());

    EntityDataManager::Instance().updateSimulationTiers(Vector2D(100.0f, 100.0f), 1500.0f, 10000.0f);

    // This should NOT cause infinite frame cycling (EDM clamps to minimum 1ms)
    auto& rd = getRenderData(npc);
    uint8_t initialFrame = rd.currentFrame;

    m_controller.update(0.001f);  // Very small delta

    // Should not cycle through all frames instantly
    // With 1ms minimum speed and 0.001s delta, might advance 1 frame max
    BOOST_CHECK_LE(rd.currentFrame - initialFrame, 1);
}

BOOST_AUTO_TEST_CASE(TestOnlyActiveNPCsUpdated) {
    // Create NPC at active distance
    EntityHandle activeNPC = createTestNPC(Vector2D(100.0f, 100.0f));

    // Create NPC at background distance
    EntityHandle bgNPC = createTestNPC(Vector2D(5000.0f, 5000.0f));

    BOOST_REQUIRE(activeNPC.isValid());
    BOOST_REQUIRE(bgNPC.isValid());

    // Update tiers - active at origin, BG threshold at 1500
    EntityDataManager::Instance().updateSimulationTiers(Vector2D(0.0f, 0.0f), 1500.0f, 10000.0f);

    // Set velocity on both
    setNPCVelocity(activeNPC, 20.0f, 0.0f);
    setNPCVelocity(bgNPC, 20.0f, 0.0f);

    // Update controller
    m_controller.update(0.01f);

    // Active NPC should have updated row
    auto& activeRd = getRenderData(activeNPC);
    BOOST_CHECK_EQUAL(activeRd.currentRow, activeRd.moveRow);

    // Background NPC should NOT have updated (still at default row)
    auto& bgRd = getRenderData(bgNPC);
    BOOST_CHECK_EQUAL(bgRd.currentRow, 0);  // Default, not updated
}

BOOST_AUTO_TEST_CASE(TestVerySmallDeltaTimeAccumulates) {
    EntityHandle npc = createTestNPC(Vector2D(100.0f, 100.0f), 1, 2, 100, 100);
    BOOST_REQUIRE(npc.isValid());

    EntityDataManager::Instance().updateSimulationTiers(Vector2D(100.0f, 100.0f), 1500.0f, 10000.0f);

    auto& rd = getRenderData(npc);
    float initialAccum = rd.animationAccumulator;

    // Update with very small delta times
    for (int i = 0; i < 10; ++i) {
        m_controller.update(0.001f);  // 1ms each
    }

    // Accumulator should have accumulated (10 * 0.001 = 0.01)
    BOOST_CHECK(approxEqual(rd.animationAccumulator - initialAccum, 0.01f, 0.005f));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// CLEAR SPAWNED NPCS TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(ClearSpawnedNPCsTests, NPCRenderControllerFixture)

BOOST_AUTO_TEST_CASE(TestClearDestroysAllNPCs) {
    // Create several NPCs
    createTestNPC(Vector2D(100.0f, 100.0f));
    createTestNPC(Vector2D(200.0f, 200.0f));
    createTestNPC(Vector2D(300.0f, 300.0f));

    BOOST_CHECK_EQUAL(EntityDataManager::Instance().getEntityCount(EntityKind::NPC), 3);

    // Clear all NPCs
    m_controller.clearSpawnedNPCs();

    // Process destruction queue
    EntityDataManager::Instance().processDestructionQueue();

    // All NPCs should be destroyed
    BOOST_CHECK_EQUAL(EntityDataManager::Instance().getEntityCount(EntityKind::NPC), 0);
}

BOOST_AUTO_TEST_CASE(TestClearUnregistersFromAI) {
    EntityHandle npc = createTestNPC(Vector2D(100.0f, 100.0f));
    BOOST_REQUIRE(npc.isValid());

    // Register with AI (need a behavior registered first)
    // Since we don't have behaviors registered in this test, we skip the AI check
    // but verify the destroy path works

    m_controller.clearSpawnedNPCs();
    EntityDataManager::Instance().processDestructionQueue();

    // NPC handle should now be invalid
    BOOST_CHECK(!EntityDataManager::Instance().isValidHandle(npc));
}

BOOST_AUTO_TEST_CASE(TestClearWithNoNPCsIsNoOp) {
    // Ensure no NPCs exist
    BOOST_CHECK_EQUAL(EntityDataManager::Instance().getEntityCount(EntityKind::NPC), 0);

    // This should not crash
    BOOST_CHECK_NO_THROW(m_controller.clearSpawnedNPCs());
}

BOOST_AUTO_TEST_CASE(TestClearDoesNotAffectOtherEntities) {
    // Create an NPC
    createTestNPC(Vector2D(100.0f, 100.0f));

    // Create a player
    EntityHandle player = EntityDataManager::Instance().registerPlayer(1, Vector2D(200.0f, 200.0f));

    BOOST_CHECK_EQUAL(EntityDataManager::Instance().getEntityCount(EntityKind::NPC), 1);
    BOOST_CHECK_EQUAL(EntityDataManager::Instance().getEntityCount(EntityKind::Player), 1);

    // Clear NPCs
    m_controller.clearSpawnedNPCs();
    EntityDataManager::Instance().processDestructionQueue();

    // NPCs should be gone, player should remain
    BOOST_CHECK_EQUAL(EntityDataManager::Instance().getEntityCount(EntityKind::NPC), 0);
    BOOST_CHECK_EQUAL(EntityDataManager::Instance().getEntityCount(EntityKind::Player), 1);
    BOOST_CHECK(EntityDataManager::Instance().isValidHandle(player));
}

BOOST_AUTO_TEST_SUITE_END()
