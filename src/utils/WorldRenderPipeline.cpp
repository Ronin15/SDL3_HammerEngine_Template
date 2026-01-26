/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "utils/WorldRenderPipeline.hpp"
#include "utils/Camera.hpp"
#include "managers/WorldManager.hpp"
#include "core/Logger.hpp"
#include <SDL3/SDL.h>
#include <cmath>
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

    // Get current camera position
    const Vector2D currentPos = camera.getPosition();

    // Calculate velocity with adaptive smoothing for stability
    if (m_hasLastPosition && deltaTime > 0.0f) {
        Vector2D instantVelocity = (currentPos - m_lastCameraPos) / deltaTime;

        // Use higher smoothing when accelerating, lower when decelerating
        // This makes prefetch respond quickly to new movement but not jitter on stops
        float instantSpeed = std::sqrt(instantVelocity.getX() * instantVelocity.getX() +
                                       instantVelocity.getY() * instantVelocity.getY());
        float currentSpeed = getCameraSpeed();

        // Accelerating: respond quickly (high smoothing factor)
        // Decelerating: smooth out gradually (lower smoothing factor)
        float adaptiveSmoothing = (instantSpeed > currentSpeed) ? 0.7f : 0.3f;

        m_cameraVelocity.setX(m_cameraVelocity.getX() * (1.0f - adaptiveSmoothing) +
                              instantVelocity.getX() * adaptiveSmoothing);
        m_cameraVelocity.setY(m_cameraVelocity.getY() * (1.0f - adaptiveSmoothing) +
                              instantVelocity.getY() * adaptiveSmoothing);
    }

    m_lastCameraPos = currentPos;
    m_hasLastPosition = true;

    // Set active camera for WorldManager (needed for updateDirtyChunks and prefetch)
    worldMgr.setActiveCamera(&camera);

    // Calculate camera speed for dynamic budget
    float speed = getCameraSpeed();

    // Prefetch chunks in movement direction with dynamic budget
    // Uses renderer stored in WorldManager (set via WorldManager::setRenderer)
    // NOTE: prefetchChunksInternal handles both prefetching AND dirty chunk updates
    worldMgr.prefetchChunksInternal(m_cameraVelocity, speed);
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

    // Add velocity information
    ctx.velocityX = m_cameraVelocity.getX();
    ctx.velocityY = m_cameraVelocity.getY();

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

float WorldRenderPipeline::getCameraSpeed() const {
    float vx = m_cameraVelocity.getX();
    float vy = m_cameraVelocity.getY();
    return std::sqrt(vx * vx + vy * vy);
}

} // namespace HammerEngine
