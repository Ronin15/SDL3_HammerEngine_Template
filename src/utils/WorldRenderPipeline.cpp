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

    // Calculate displacement since last frame
    float dx = currentPos.getX() - m_lastCameraPos.getX();
    float dy = currentPos.getY() - m_lastCameraPos.getY();

    // OPTIMIZATION: Skip velocity calculation if camera barely moved
    constexpr float MOVEMENT_EPSILON_SQ = 0.25f;  // 0.5 pixel squared
    bool cameraMoved = (dx * dx + dy * dy) > MOVEMENT_EPSILON_SQ;

    if (m_hasLastPosition && cameraMoved) {
        // Calculate instant velocity
        float invDt = 1.0f / deltaTime;
        float instantVelX = dx * invDt;
        float instantVelY = dy * invDt;
        float instantSpeedSq = instantVelX * instantVelX + instantVelY * instantVelY;
        float currentSpeedSq = m_cameraVelocity.getX() * m_cameraVelocity.getX() +
                               m_cameraVelocity.getY() * m_cameraVelocity.getY();

        // Adaptive smoothing: fast response to acceleration, gradual deceleration
        float adaptiveSmoothing = (instantSpeedSq > currentSpeedSq) ? 0.7f : 0.3f;
        float oneMinusSmooth = 1.0f - adaptiveSmoothing;

        m_cameraVelocity.setX(m_cameraVelocity.getX() * oneMinusSmooth + instantVelX * adaptiveSmoothing);
        m_cameraVelocity.setY(m_cameraVelocity.getY() * oneMinusSmooth + instantVelY * adaptiveSmoothing);

        m_lastCameraPos = currentPos;
    } else if (!m_hasLastPosition) {
        m_lastCameraPos = currentPos;
        m_hasLastPosition = true;
    }
    // NOTE: When stationary, we keep the existing velocity (will decay via prefetch logic)

    m_hasLastPosition = true;

    // Only update active camera if it changed (avoid redundant pointer assignment)
    worldMgr.setActiveCamera(&camera);

    // Prefetch with cached speed calculation
    worldMgr.prefetchChunksInternal(m_cameraVelocity, getCameraSpeed());
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
