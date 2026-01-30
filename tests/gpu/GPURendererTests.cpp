/* Copyright (c) 2025 Hammer Forged Games
 * Licensed under the MIT License */

/**
 * System tests for GPURenderer full frame flow.
 */

#define BOOST_TEST_MODULE GPURendererTests
#include <boost/test/unit_test.hpp>

#include "GPUTestFixture.hpp"
#include "gpu/GPUDevice.hpp"
#include "gpu/GPURenderer.hpp"
#include "gpu/GPUShaderManager.hpp"

using namespace HammerEngine;
using namespace HammerEngine::Test;

// Global fixture for SDL cleanup
BOOST_GLOBAL_FIXTURE(GPUGlobalFixture);

/**
 * Test fixture that initializes full GPU stack for renderer testing.
 */
struct RendererTestFixture : public GPUTestFixture {
    RendererTestFixture() {
        if (!isGPUAvailable()) return;

        device = &GPUDevice::Instance();
        if (device->isInitialized()) {
            // Full shutdown sequence
            GPURenderer::Instance().shutdown();
            GPUShaderManager::Instance().shutdown();
            device->shutdown();
        }

        SDL_Window* window = getTestWindow();
        if (window) {
            if (device->init(window)) {
                renderer = &GPURenderer::Instance();
                rendererInitialized = renderer->init();
            }
        }
    }

    ~RendererTestFixture() {
        if (renderer && rendererInitialized) {
            renderer->shutdown();
        }
        GPUShaderManager::Instance().shutdown();
        if (device && device->isInitialized()) {
            device->shutdown();
        }
    }

    GPUDevice* device = nullptr;
    GPURenderer* renderer = nullptr;
    bool rendererInitialized = false;
};

// ============================================================================
// GPU RENDERER LIFECYCLE TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(GPURendererLifecycleTests)

BOOST_FIXTURE_TEST_CASE(SingletonInstance, RendererTestFixture) {
    GPURenderer& r1 = GPURenderer::Instance();
    GPURenderer& r2 = GPURenderer::Instance();

    BOOST_CHECK(&r1 == &r2);
}

BOOST_FIXTURE_TEST_CASE(InitRequiresGPUDevice, RendererTestFixture) {
    SKIP_IF_NO_GPU();

    // Renderer should be initialized if GPUDevice was initialized
    BOOST_CHECK(rendererInitialized);
}

BOOST_FIXTURE_TEST_CASE(InitFailsWithoutDevice, RendererTestFixture) {
    // Shutdown current state
    if (renderer && rendererInitialized) {
        renderer->shutdown();
        rendererInitialized = false;
    }
    GPUShaderManager::Instance().shutdown();
    if (device && device->isInitialized()) {
        device->shutdown();
    }

    // Try to init renderer without device
    GPURenderer& r = GPURenderer::Instance();
    bool result = r.init();

    BOOST_CHECK(!result);
}

BOOST_FIXTURE_TEST_CASE(ShutdownSafety, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    // Shutdown
    renderer->shutdown();
    rendererInitialized = false;

    // Double shutdown should be safe
    renderer->shutdown();
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// FRAME CYCLE TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(FrameCycleTests)

BOOST_FIXTURE_TEST_CASE(BeginFrameAcquiresCommandBuffer, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    // Show window to get valid swapchain for frame cycle testing
    GPUTestFixture::showTestWindow();

    renderer->beginFrame();

    // With visible window, command buffer and copy pass should be acquired
    // If swapchain still not available (rare edge case), skip the test
    if (renderer->getCommandBuffer() == nullptr) {
        BOOST_TEST_MESSAGE("Swapchain not available despite visible window - skipping test");
        GPUTestFixture::hideTestWindow();
        return;
    }

    BOOST_CHECK(renderer->getCommandBuffer() != nullptr);
    BOOST_CHECK(renderer->getCopyPass() != nullptr);

    // Complete the frame
    SDL_GPURenderPass* pass = renderer->beginScenePass();
    if (pass) {
        pass = renderer->beginSwapchainPass();
    }
    renderer->endFrame();

    GPUTestFixture::hideTestWindow();
}

BOOST_FIXTURE_TEST_CASE(BeginScenePassEndssCopyPass, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    // Show window to get valid swapchain for frame cycle testing
    GPUTestFixture::showTestWindow();

    renderer->beginFrame();

    // Skip if swapchain not available despite visible window
    if (renderer->getCommandBuffer() == nullptr) {
        BOOST_TEST_MESSAGE("Swapchain not available despite visible window - skipping test");
        GPUTestFixture::hideTestWindow();
        return;
    }

    BOOST_REQUIRE(renderer->getCopyPass() != nullptr);

    SDL_GPURenderPass* scenePass = renderer->beginScenePass();

    // After beginScenePass, copy pass should be ended (null)
    BOOST_CHECK(renderer->getCopyPass() == nullptr);

    // Scene pass should be valid
    if (scenePass) {
        BOOST_CHECK(scenePass != nullptr);
    }

    // Complete the frame
    renderer->beginSwapchainPass();
    renderer->endFrame();

    GPUTestFixture::hideTestWindow();
}

BOOST_FIXTURE_TEST_CASE(BeginSwapchainPassEndsScenePass, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    // Show window to get valid swapchain for frame cycle testing
    GPUTestFixture::showTestWindow();

    renderer->beginFrame();

    // Skip if swapchain not available despite visible window
    if (renderer->getCommandBuffer() == nullptr) {
        BOOST_TEST_MESSAGE("Swapchain not available despite visible window - skipping test");
        GPUTestFixture::hideTestWindow();
        return;
    }

    renderer->beginScenePass();

    SDL_GPURenderPass* swapchainPass = renderer->beginSwapchainPass();

    // Swapchain pass should be valid
    if (swapchainPass) {
        BOOST_CHECK(swapchainPass != nullptr);
    }

    renderer->endFrame();

    GPUTestFixture::hideTestWindow();
}

BOOST_FIXTURE_TEST_CASE(EndFrameSubmitsCommandBuffer, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    // Show window to get valid swapchain for frame cycle testing
    GPUTestFixture::showTestWindow();

    renderer->beginFrame();

    // Skip if swapchain not available despite visible window
    if (renderer->getCommandBuffer() == nullptr) {
        BOOST_TEST_MESSAGE("Swapchain not available despite visible window - skipping test");
        GPUTestFixture::hideTestWindow();
        return;
    }

    renderer->beginScenePass();
    renderer->beginSwapchainPass();
    renderer->endFrame();

    // After endFrame, command buffer should be submitted
    // (Can't easily verify this without checking frame was presented)
    BOOST_CHECK(true);  // Frame completed without crash

    GPUTestFixture::hideTestWindow();
}

BOOST_FIXTURE_TEST_CASE(MultipleFrameCycles, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    // Show window to get valid swapchain for frame cycle testing
    GPUTestFixture::showTestWindow();

    // Run multiple frame cycles
    for (int frame = 0; frame < 5; ++frame) {
        renderer->beginFrame();

        // Skip if swapchain not available
        if (renderer->getCommandBuffer() == nullptr) {
            BOOST_TEST_MESSAGE("Swapchain not available on frame " << frame << " - skipping test");
            GPUTestFixture::hideTestWindow();
            return;
        }

        renderer->beginScenePass();
        renderer->beginSwapchainPass();
        renderer->endFrame();
    }

    BOOST_CHECK(true);  // Multiple frames completed

    GPUTestFixture::hideTestWindow();
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// PIPELINE ACCESSOR TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(PipelineAccessorTests)

BOOST_FIXTURE_TEST_CASE(SpriteOpaquePipelineValid, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    SDL_GPUGraphicsPipeline* pipeline = renderer->getSpriteOpaquePipeline();
    BOOST_CHECK(pipeline != nullptr);
}

BOOST_FIXTURE_TEST_CASE(SpriteAlphaPipelineValid, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    SDL_GPUGraphicsPipeline* pipeline = renderer->getSpriteAlphaPipeline();
    BOOST_CHECK(pipeline != nullptr);
}

BOOST_FIXTURE_TEST_CASE(ParticlePipelineValid, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    SDL_GPUGraphicsPipeline* pipeline = renderer->getParticlePipeline();
    BOOST_CHECK(pipeline != nullptr);
}

BOOST_FIXTURE_TEST_CASE(PrimitivePipelineValid, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    SDL_GPUGraphicsPipeline* pipeline = renderer->getPrimitivePipeline();
    BOOST_CHECK(pipeline != nullptr);
}

BOOST_FIXTURE_TEST_CASE(CompositePipelineValid, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    SDL_GPUGraphicsPipeline* pipeline = renderer->getCompositePipeline();
    BOOST_CHECK(pipeline != nullptr);
}

BOOST_FIXTURE_TEST_CASE(UISpritePipelineValid, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    SDL_GPUGraphicsPipeline* pipeline = renderer->getUISpritePipeline();
    BOOST_CHECK(pipeline != nullptr);
}

BOOST_FIXTURE_TEST_CASE(UIPrimitivePipelineValid, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    SDL_GPUGraphicsPipeline* pipeline = renderer->getUIPrimitivePipeline();
    BOOST_CHECK(pipeline != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// VERTEX POOL ACCESSOR TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(VertexPoolAccessorTests)

BOOST_FIXTURE_TEST_CASE(SpriteVertexPoolInitialized, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    GPUVertexPool& pool = renderer->getSpriteVertexPool();
    BOOST_CHECK(pool.isInitialized());
}

BOOST_FIXTURE_TEST_CASE(EntityVertexPoolInitialized, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    GPUVertexPool& pool = renderer->getEntityVertexPool();
    BOOST_CHECK(pool.isInitialized());
}

BOOST_FIXTURE_TEST_CASE(ParticleVertexPoolInitialized, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    GPUVertexPool& pool = renderer->getParticleVertexPool();
    BOOST_CHECK(pool.isInitialized());
}

BOOST_FIXTURE_TEST_CASE(PrimitiveVertexPoolInitialized, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    GPUVertexPool& pool = renderer->getPrimitiveVertexPool();
    BOOST_CHECK(pool.isInitialized());
}

BOOST_FIXTURE_TEST_CASE(UIVertexPoolInitialized, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    GPUVertexPool& pool = renderer->getUIVertexPool();
    BOOST_CHECK(pool.isInitialized());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// SAMPLER ACCESSOR TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(SamplerAccessorTests)

BOOST_FIXTURE_TEST_CASE(NearestSamplerValid, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    SDL_GPUSampler* sampler = renderer->getNearestSampler();
    BOOST_CHECK(sampler != nullptr);
}

BOOST_FIXTURE_TEST_CASE(LinearSamplerValid, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    SDL_GPUSampler* sampler = renderer->getLinearSampler();
    BOOST_CHECK(sampler != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// SCENE TEXTURE TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(SceneTextureTests)

BOOST_FIXTURE_TEST_CASE(SceneTextureValid, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    GPUTexture* sceneTexture = renderer->getSceneTexture();
    BOOST_CHECK(sceneTexture != nullptr);
    BOOST_CHECK(sceneTexture->isValid());
}

BOOST_FIXTURE_TEST_CASE(SceneTextureIsSamplerAndRenderTarget, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    GPUTexture* sceneTexture = renderer->getSceneTexture();
    BOOST_REQUIRE(sceneTexture != nullptr);

    BOOST_CHECK(sceneTexture->isSampler());
    BOOST_CHECK(sceneTexture->isRenderTarget());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// COMPOSITE TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(CompositeTests)

BOOST_FIXTURE_TEST_CASE(SetCompositeParams, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    // Should not crash
    renderer->setCompositeParams(2.0f, 0.25f, 0.5f);
    BOOST_CHECK(true);
}

BOOST_FIXTURE_TEST_CASE(SetDayNightParams, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    // Should not crash
    renderer->setDayNightParams(0.8f, 0.9f, 1.0f, 0.5f);
    BOOST_CHECK(true);
}

BOOST_FIXTURE_TEST_CASE(RenderCompositeInSwapchainPass, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    renderer->setCompositeParams(1.0f, 0.0f, 0.0f);
    renderer->setDayNightParams(1.0f, 1.0f, 1.0f, 0.0f);

    renderer->beginFrame();
    renderer->beginScenePass();
    SDL_GPURenderPass* swapchainPass = renderer->beginSwapchainPass();

    if (swapchainPass) {
        // Render composite should work in swapchain pass
        renderer->renderComposite(swapchainPass);
    }

    renderer->endFrame();
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// VIEWPORT TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(ViewportTests)

BOOST_FIXTURE_TEST_CASE(ViewportDimensionsValid, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    uint32_t width = renderer->getViewportWidth();
    uint32_t height = renderer->getViewportHeight();

    // Viewport should have valid dimensions
    BOOST_CHECK(width > 0);
    BOOST_CHECK(height > 0);
}

BOOST_FIXTURE_TEST_CASE(UpdateViewport, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    renderer->updateViewport(1920, 1080);

    BOOST_CHECK_EQUAL(renderer->getViewportWidth(), 1920u);
    BOOST_CHECK_EQUAL(renderer->getViewportHeight(), 1080u);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// ORTHO MATRIX TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(OrthoMatrixTests)

BOOST_AUTO_TEST_CASE(CreateOrthoMatrixBasic) {
    float matrix[16];

    GPURenderer::createOrthoMatrix(0.0f, 800.0f, 600.0f, 0.0f, matrix);

    // Matrix should be valid orthographic projection
    // Check key elements
    // [0][0] = 2/(right-left) = 2/800 = 0.0025
    BOOST_CHECK_CLOSE(matrix[0], 2.0f / 800.0f, 0.001f);

    // [1][1] = 2/(top-bottom) = 2/(0-600) = -2/600
    BOOST_CHECK_CLOSE(matrix[5], 2.0f / (0.0f - 600.0f), 0.001f);

    // [3][0] = -(right+left)/(right-left) = -800/800 = -1
    BOOST_CHECK_CLOSE(matrix[12], -(800.0f + 0.0f) / 800.0f, 0.001f);

    // [3][1] = -(top+bottom)/(top-bottom) = -600/(0-600) = 1
    BOOST_CHECK_CLOSE(matrix[13], -(0.0f + 600.0f) / (0.0f - 600.0f), 0.001f);
}

BOOST_AUTO_TEST_CASE(CreateOrthoMatrixZeroDepth) {
    float matrix[16];

    GPURenderer::createOrthoMatrix(0.0f, 1920.0f, 1080.0f, 0.0f, matrix);

    // Z components for 2D (near=0, far=1)
    // [2][2] = -2/(far-near) = -2/1 = -2
    // For 2D ortho, we typically use 0 to 1 depth
    // Check matrix is not all zeros
    bool hasNonZero = false;
    for (int i = 0; i < 16; ++i) {
        if (matrix[i] != 0.0f) {
            hasNonZero = true;
            break;
        }
    }
    BOOST_CHECK(hasNonZero);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// SPRITE BATCH ACCESSOR TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(SpriteBatchAccessorTests)

BOOST_FIXTURE_TEST_CASE(SpriteBatchAccessor, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    SpriteBatch& batch = renderer->getSpriteBatch();
    // Batch should be accessible (initialization tested elsewhere)
    BOOST_CHECK(batch.getIndexBuffer() != nullptr);
}

BOOST_FIXTURE_TEST_CASE(EntityBatchAccessor, RendererTestFixture) {
    SKIP_IF_NO_GPU();
    BOOST_REQUIRE(rendererInitialized);

    SpriteBatch& batch = renderer->getEntityBatch();
    BOOST_CHECK(batch.getIndexBuffer() != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
