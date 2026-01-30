/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "utils/WorldRenderPipeline.hpp"
#include "utils/Camera.hpp"
#include "managers/WorldManager.hpp"
#include "core/Logger.hpp"
#include <SDL3/SDL.h>
#include <format>

namespace HammerEngine {

WorldRenderPipeline::WorldRenderPipeline()
    : m_sceneRenderer(std::make_unique<SceneRenderer>())
{
}

WorldRenderPipeline::~WorldRenderPipeline() = default;

WorldRenderPipeline::WorldRenderPipeline(WorldRenderPipeline&&) noexcept = default;

WorldRenderPipeline& WorldRenderPipeline::operator=(WorldRenderPipeline&&) noexcept = default;

void WorldRenderPipeline::prepareChunks(Camera& camera, float deltaTime) {
    if (deltaTime <= 0.0f) {
        return;
    }

    auto& worldMgr = WorldManager::Instance();
    if (!worldMgr.isInitialized() || !worldMgr.hasActiveWorld()) {
        return;
    }

    // Set active camera for chunk visibility calculations
    worldMgr.setActiveCamera(&camera);

    // Process dirty chunks (season changes, etc.) with proper render target management
    worldMgr.prefetchChunksInternal();
}

WorldRenderPipeline::RenderContext WorldRenderPipeline::beginScene(
    SDL_Renderer* renderer,
    Camera& camera,
    float interpolationAlpha
) {
    RenderContext ctx;

    if (!renderer || !m_sceneRenderer) {
        return ctx;
    }

    // Delegate to SceneRenderer for actual texture setup
    auto sceneCtx = m_sceneRenderer->beginScene(renderer, camera, interpolationAlpha);

    if (!sceneCtx.valid) {
        return ctx;
    }

    // Copy all fields from SceneRenderer context
    ctx.cameraX = sceneCtx.cameraX;
    ctx.cameraY = sceneCtx.cameraY;
    ctx.flooredCameraX = sceneCtx.flooredCameraX;
    ctx.flooredCameraY = sceneCtx.flooredCameraY;
    ctx.viewWidth = sceneCtx.viewWidth;
    ctx.viewHeight = sceneCtx.viewHeight;
    ctx.zoom = sceneCtx.zoom;
    ctx.cameraCenter = sceneCtx.cameraCenter;

    ctx.valid = true;
    return ctx;
}

void WorldRenderPipeline::renderWorld(SDL_Renderer* renderer, const RenderContext& ctx) {
    if (!renderer || !ctx.valid) {
        return;
    }

    auto& worldMgr = WorldManager::Instance();
    if (!worldMgr.isInitialized() || !worldMgr.hasActiveWorld()) {
        return;
    }

    // Render tiles using the pre-computed context
    worldMgr.render(renderer, ctx.flooredCameraX, ctx.flooredCameraY,
                    ctx.viewWidth, ctx.viewHeight);
}

void WorldRenderPipeline::endScene(SDL_Renderer* renderer) {
    if (!renderer || !m_sceneRenderer) {
        return;
    }

    m_sceneRenderer->endScene(renderer);
}

void WorldRenderPipeline::prewarmVisibleChunks(
    SDL_Renderer* renderer,
    float centerX,
    float centerY,
    float viewWidth,
    float viewHeight
) {
    if (!renderer) {
        return;
    }

    auto& worldMgr = WorldManager::Instance();
    if (!worldMgr.isInitialized() || !worldMgr.hasActiveWorld()) {
        return;
    }

    WORLD_RENDER_PIPELINE_INFO(std::format(
        "Pre-warming chunks at ({:.0f}, {:.0f}) with view {}x{}",
        centerX, centerY, viewWidth, viewHeight));

    // Calculate camera offset (top-left corner) from center
    float cameraX = centerX - viewWidth / 2.0f;
    float cameraY = centerY - viewHeight / 2.0f;

    // Prewarm visible chunks (renders all chunks in view, no budget limit)
    worldMgr.prewarmChunks(renderer, cameraX, cameraY, viewWidth, viewHeight);

    WORLD_RENDER_PIPELINE_INFO("Chunk pre-warm complete");
}

bool WorldRenderPipeline::isSceneActive() const {
    return m_sceneRenderer && m_sceneRenderer->isSceneActive();
}

} // namespace HammerEngine
