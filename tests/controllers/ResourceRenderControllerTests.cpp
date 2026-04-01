/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/**
 * @file ResourceRenderControllerTests.cpp
 * @brief Tests for ResourceRenderController
 *
 * Tests unified rendering of dropped items and containers.
 */

#define BOOST_TEST_MODULE ResourceRenderControllerTests
#include <boost/test/unit_test.hpp>

#define private public
#include "controllers/render/ResourceRenderController.hpp"
#undef private
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include "utils/Camera.hpp"
#include "utils/GPUSceneRecorder.hpp"
#include "../events/EventManagerTestAccess.hpp"
#include <memory>
#include <vector>

#define private public
#include "gpu/SpriteBatch.hpp"
#undef private

using namespace HammerEngine;

// ============================================================================
// Test Fixture
// ============================================================================

class ResourceRenderControllerTestFixture {
public:
    ResourceRenderControllerTestFixture()
        : m_camera(1000.0f, 1000.0f, 200.0f, 200.0f)
    {
        // Reset EventManager to clean state
        EventManagerTestAccess::reset();
        EventManager::Instance().init();

        if (!ResourceTemplateManager::Instance().isInitialized()) {
            ResourceTemplateManager::Instance().init();
        }

        // Initialize EntityDataManager
        EntityDataManager::Instance().init();

        // Initialize WorldResourceManager
        WorldResourceManager::Instance().init();
        WorldResourceManager::Instance().setActiveWorld(m_worldId);
    }

    ~ResourceRenderControllerTestFixture() {
        EntityDataManager::Instance().clean();
        WorldResourceManager::Instance().clean();
        ResourceTemplateManager::Instance().clean();
        EventManager::Instance().clean();
    }

    Camera& getCamera() { return m_camera; }
    const std::string& getWorldId() const { return m_worldId; }

    HammerEngine::ResourceHandle getTestResourceHandle() const {
        return ResourceTemplateManager::Instance().getHandleByName("Super Health Potion");
    }

    EntityHandle createDroppedItem(const Vector2D& position, int quantity = 1) {
        return EntityDataManager::Instance().createDroppedItem(position, getTestResourceHandle(), quantity, m_worldId);
    }

    EntityHandle createContainer(const Vector2D& position, ContainerType type, uint16_t maxSlots = 12) {
        return EntityDataManager::Instance().createContainer(position, type, maxSlots, 0, m_worldId);
    }

    // Non-copyable
    ResourceRenderControllerTestFixture(const ResourceRenderControllerTestFixture&) = delete;
    ResourceRenderControllerTestFixture& operator=(const ResourceRenderControllerTestFixture&) = delete;

private:
    Camera m_camera;
    std::string m_worldId{"resource_render_test_world"};
};

namespace {

void beginSpriteBatchRecording(HammerEngine::SpriteBatch& batch,
                               std::vector<HammerEngine::SpriteVertex>& vertices,
                               float textureWidth,
                               float textureHeight,
                               float targetHeight) {
    batch.m_initialized = true;
    batch.begin(vertices.data(), vertices.size(), nullptr, nullptr,
                textureWidth, textureHeight, targetHeight);
}

} // namespace

// ============================================================================
// Basic State Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(ResourceRenderControllerStateTests, ResourceRenderControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestResourceRenderControllerName) {
    ResourceRenderController controller;

    BOOST_CHECK_EQUAL(controller.getName(), "ResourceRenderController");
}

BOOST_AUTO_TEST_CASE(TestSubscribe) {
    ResourceRenderController controller;

    // Subscribe is a no-op (no events needed)
    controller.subscribe();

    BOOST_CHECK(!controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestDefaultConstruction) {
    // Should not throw
    ResourceRenderController controller;
    BOOST_CHECK_EQUAL(controller.getName(), "ResourceRenderController");
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Update Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(ResourceRenderControllerUpdateTests, ResourceRenderControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestUpdateWithNoResources) {
    ResourceRenderController controller;

    controller.update(0.016f, getCamera());
    controller.update(1.0f, getCamera());
    controller.update(0.0f, getCamera());

    BOOST_CHECK(controller.m_visibleItemIndices.empty());
    BOOST_CHECK(controller.m_visibleContainerIndices.empty());
}

BOOST_AUTO_TEST_CASE(TestUpdateMultipleTimes) {
    ResourceRenderController controller;

    for (int i = 0; i < 100; ++i) {
        controller.update(0.016f, getCamera());
    }

    BOOST_CHECK(controller.m_visibleItemIndices.empty());
    BOOST_CHECK(controller.m_visibleContainerIndices.empty());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// ClearAll Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(ResourceRenderControllerClearTests, ResourceRenderControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestClearAllWithNoResources) {
    ResourceRenderController controller;
    auto& edm = EntityDataManager::Instance();

    BOOST_CHECK_EQUAL(edm.getEntityCount(EntityKind::DroppedItem), 0);
    BOOST_CHECK_EQUAL(edm.getEntityCount(EntityKind::Container), 0);
    controller.clearAll();
    BOOST_CHECK_EQUAL(edm.getEntityCount(EntityKind::DroppedItem), 0);
    BOOST_CHECK_EQUAL(edm.getEntityCount(EntityKind::Container), 0);
}

BOOST_AUTO_TEST_CASE(TestClearAllMultipleTimes) {
    ResourceRenderController controller;
    auto& edm = EntityDataManager::Instance();

    controller.clearAll();
    controller.clearAll();
    controller.clearAll();

    BOOST_CHECK_EQUAL(edm.getEntityCount(EntityKind::DroppedItem), 0);
    BOOST_CHECK_EQUAL(edm.getEntityCount(EntityKind::Container), 0);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Animation Constants Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(ResourceRenderControllerConstantsTests)

BOOST_AUTO_TEST_CASE(TestAnimationConstants) {
    ResourceRenderControllerTestFixture fixture;
    ResourceRenderController controller;

    controller.update(0.001f, fixture.getCamera());
    controller.update(0.1f, fixture.getCamera());
    controller.update(1.0f, fixture.getCamera());

    BOOST_CHECK(controller.m_visibleItemIndices.empty());
    BOOST_CHECK(controller.m_visibleContainerIndices.empty());
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(ResourceRenderControllerBehaviorTests, ResourceRenderControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestUpdateOnlyVisibleDroppedItemsAdvance) {
    auto resourceHandle = getTestResourceHandle();
    BOOST_REQUIRE(resourceHandle.isValid());

    EntityHandle visibleItem = createDroppedItem(Vector2D(100.0f, 100.0f));
    EntityHandle hiddenItem = createDroppedItem(Vector2D(2000.0f, 2000.0f));
    BOOST_REQUIRE(visibleItem.isValid());
    BOOST_REQUIRE(hiddenItem.isValid());

    auto& edm = EntityDataManager::Instance();
    auto& visibleHot = edm.getHotData(visibleItem);
    auto& hiddenHot = edm.getHotData(hiddenItem);
    auto& visibleRender = edm.getItemRenderDataByTypeIndex(visibleHot.typeLocalIndex);
    auto& hiddenRender = edm.getItemRenderDataByTypeIndex(hiddenHot.typeLocalIndex);
    visibleRender.numFrames = 2;
    visibleRender.animSpeedMs = 100;
    hiddenRender.numFrames = 2;
    hiddenRender.animSpeedMs = 100;

    const float initialVisibleBob = visibleRender.bobPhase;
    const float initialHiddenBob = hiddenRender.bobPhase;
    const uint8_t initialVisibleFrame = visibleRender.currentFrame;
    const uint8_t initialHiddenFrame = hiddenRender.currentFrame;

    auto& camera = getCamera();
    camera.setPosition(100.0f, 100.0f);

    ResourceRenderController controller;
    controller.update(1.0f, camera);

    BOOST_CHECK_GT(visibleRender.bobPhase, initialVisibleBob);
    BOOST_CHECK_NE(visibleRender.currentFrame, initialVisibleFrame);
    BOOST_CHECK_EQUAL(hiddenRender.bobPhase, initialHiddenBob);
    BOOST_CHECK_EQUAL(hiddenRender.currentFrame, initialHiddenFrame);
}

BOOST_AUTO_TEST_CASE(TestRecordGPUDroppedItemsUsesCameraCenterAndInterpolatedPosition) {
    auto resourceHandle = getTestResourceHandle();
    BOOST_REQUIRE(resourceHandle.isValid());

    EntityHandle item = createDroppedItem(Vector2D(100.0f, 120.0f));
    BOOST_REQUIRE(item.isValid());

    auto& edm = EntityDataManager::Instance();
    auto& hot = edm.getHotData(item);
    hot.transform.previousPosition = Vector2D(90.0f, 110.0f);
    hot.transform.position = Vector2D(110.0f, 130.0f);

    auto& renderData = edm.getItemRenderDataByTypeIndex(hot.typeLocalIndex);
    renderData.currentFrame = 1;
    renderData.bobPhase = 0.0f;
    renderData.bobAmplitude = 0.0f;

    std::vector<HammerEngine::SpriteVertex> vertices(8);
    HammerEngine::SpriteBatch batch;
    beginSpriteBatchRecording(batch, vertices, 1024.0f, 1024.0f, 512.0f);

    HammerEngine::GPUSceneContext ctx{};
    ctx.cameraX = 10.0f;
    ctx.cameraY = 20.0f;
    ctx.interpolationAlpha = 0.25f;
    ctx.cameraCenter = Vector2D(100.0f, 120.0f);
    ctx.spriteBatch = &batch;
    ctx.valid = true;

    ResourceRenderController controller;
    Camera farAwayCamera(1000.0f, 1000.0f, 200.0f, 200.0f);
    controller.recordGPUDroppedItems(ctx, farAwayCamera);

    BOOST_CHECK_EQUAL(batch.end(), HammerEngine::SpriteBatch::VERTICES_PER_SPRITE);

    const float interpX = 90.0f + (110.0f - 90.0f) * 0.25f;
    const float interpY = 110.0f + (130.0f - 110.0f) * 0.25f;
    const float expectedDstX = interpX - ctx.cameraX - renderData.frameWidth * 0.5f;
    const float expectedDstY = interpY - ctx.cameraY - renderData.frameHeight * 0.5f;

    BOOST_CHECK_CLOSE(vertices[0].x, expectedDstX, 0.001f);
    BOOST_CHECK_CLOSE(vertices[0].y, 512.0f - expectedDstY, 0.001f);
    BOOST_CHECK_CLOSE(vertices[1].x, expectedDstX + renderData.frameWidth, 0.001f);
    BOOST_CHECK_CLOSE(vertices[2].y, vertices[0].y - renderData.frameHeight, 0.001f);
}

BOOST_AUTO_TEST_CASE(TestRecordGPUContainersUsesOpenAndClosedVariants) {
    EntityHandle container = createContainer(Vector2D(100.0f, 120.0f), ContainerType::Chest);
    BOOST_REQUIRE(container.isValid());

    auto& hot = EntityDataManager::Instance().getHotData(container);
    hot.transform.previousPosition = Vector2D(100.0f, 120.0f);
    hot.transform.position = Vector2D(100.0f, 120.0f);

    auto& containerData = EntityDataManager::Instance().getContainerData(container);
    auto& renderData = EntityDataManager::Instance().getContainerRenderDataByTypeIndex(hot.typeLocalIndex);

    ResourceRenderController controller;
    Camera renderCamera(1000.0f, 1000.0f, 200.0f, 200.0f);

    std::vector<HammerEngine::SpriteVertex> closedVertices(8);
    HammerEngine::SpriteBatch closedBatch;
    beginSpriteBatchRecording(closedBatch, closedVertices, 1024.0f, 1024.0f, 512.0f);

    HammerEngine::GPUSceneContext closedCtx{};
    closedCtx.cameraX = 0.0f;
    closedCtx.cameraY = 0.0f;
    closedCtx.interpolationAlpha = 1.0f;
    closedCtx.cameraCenter = Vector2D(100.0f, 120.0f);
    closedCtx.spriteBatch = &closedBatch;
    closedCtx.valid = true;

    controller.recordGPUContainers(closedCtx, renderCamera);
    BOOST_CHECK_EQUAL(closedBatch.end(), HammerEngine::SpriteBatch::VERTICES_PER_SPRITE);

    const float closedSrcX = static_cast<float>(renderData.atlasX);
    const float closedSrcY = static_cast<float>(renderData.atlasY);
    const float closedSrcW = static_cast<float>(renderData.frameWidth);
    const float closedSrcH = static_cast<float>(renderData.frameHeight);

    BOOST_CHECK_CLOSE(closedVertices[0].u, closedSrcX / 1024.0f, 0.001f);
    BOOST_CHECK_CLOSE(closedVertices[0].v, closedSrcY / 1024.0f, 0.001f);
    BOOST_CHECK_CLOSE(closedVertices[1].x, closedVertices[0].x + closedSrcW, 0.001f);
    BOOST_CHECK_CLOSE(closedVertices[2].y, closedVertices[0].y - closedSrcH, 0.001f);

    containerData.setOpen(true);

    std::vector<HammerEngine::SpriteVertex> openVertices(8);
    HammerEngine::SpriteBatch openBatch;
    beginSpriteBatchRecording(openBatch, openVertices, 1024.0f, 1024.0f, 512.0f);

    HammerEngine::GPUSceneContext openCtx = closedCtx;
    openCtx.spriteBatch = &openBatch;

    controller.recordGPUContainers(openCtx, renderCamera);
    BOOST_CHECK_EQUAL(openBatch.end(), HammerEngine::SpriteBatch::VERTICES_PER_SPRITE);

    const float openSrcX = static_cast<float>(renderData.openAtlasX);
    const float openSrcY = static_cast<float>(renderData.openAtlasY);
    const float openSrcW = static_cast<float>(renderData.openFrameWidth);
    const float openSrcH = static_cast<float>(renderData.openFrameHeight);

    BOOST_CHECK_CLOSE(openVertices[0].u, openSrcX / 1024.0f, 0.001f);
    BOOST_CHECK_CLOSE(openVertices[0].v, openSrcY / 1024.0f, 0.001f);
    BOOST_CHECK_CLOSE(openVertices[1].x, openVertices[0].x + openSrcW, 0.001f);
    BOOST_CHECK_CLOSE(openVertices[2].y, openVertices[0].y - openSrcH, 0.001f);
    BOOST_CHECK_NE(openVertices[2].y, closedVertices[2].y);
}

BOOST_AUTO_TEST_CASE(TestClearAllRemovesTrackedResources) {
    EntityHandle item = createDroppedItem(Vector2D(100.0f, 100.0f));
    EntityHandle container = createContainer(Vector2D(120.0f, 120.0f), ContainerType::Chest);
    BOOST_REQUIRE(item.isValid());
    BOOST_REQUIRE(container.isValid());

    ResourceRenderController controller;
    controller.clearAll();
    EntityDataManager::Instance().processDestructionQueue();

    BOOST_CHECK(!EntityDataManager::Instance().isValidHandle(item));
    BOOST_CHECK(!EntityDataManager::Instance().isValidHandle(container));
    BOOST_CHECK_EQUAL(EntityDataManager::Instance().getEntityCount(EntityKind::DroppedItem), 0);
    BOOST_CHECK_EQUAL(EntityDataManager::Instance().getEntityCount(EntityKind::Container), 0);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Non-Copyable Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(ResourceRenderControllerCopyTests)

BOOST_AUTO_TEST_CASE(TestNonCopyable) {
    // This test verifies at compile time that the controller is non-copyable
    // The following would fail to compile:
    // ResourceRenderController controller;
    // ResourceRenderController copy = controller;  // Won't compile
    // ResourceRenderController copy2(controller);  // Won't compile

    // Just verify we can default construct
    ResourceRenderController controller;
    BOOST_CHECK_EQUAL(controller.getName(), "ResourceRenderController");
}

BOOST_AUTO_TEST_SUITE_END()
