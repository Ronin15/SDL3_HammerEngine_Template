/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "utils/SceneRenderer.hpp"
#include "utils/Camera.hpp"
#include "core/Logger.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <format>

namespace HammerEngine {

SceneRenderer::SceneRenderer() = default;

SceneRenderer::~SceneRenderer() = default;

SceneRenderer::SceneRenderer(SceneRenderer&&) noexcept = default;

SceneRenderer& SceneRenderer::operator=(SceneRenderer&&) noexcept = default;

bool SceneRenderer::ensureTextureSize(SDL_Renderer* renderer, int width, int height) {
    if (m_intermediateTexture && m_textureWidth >= width && m_textureHeight >= height) {
        return true;
    }

    // Create new texture with required dimensions
    SDL_Texture* tex = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        width,
        height
    );

    if (!tex) {
        SCENE_RENDERER_ERROR( std::format("Failed to create intermediate texture {}x{}: {}",
                               width, height, SDL_GetError()));
        return false;
    }

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);  // Pixel-perfect, no bilinear filtering

    m_intermediateTexture = std::shared_ptr<SDL_Texture>(tex, SDL_DestroyTexture);
    m_textureWidth = width;
    m_textureHeight = height;

    SCENE_RENDERER_DEBUG( std::format("Created intermediate texture {}x{}", width, height));
    return true;
}

SceneRenderer::SceneContext SceneRenderer::beginScene(
    SDL_Renderer* renderer,
    Camera& camera,
    float interpolationAlpha
) {
    SceneContext ctx;

    if (!renderer) {
        SCENE_RENDERER_ERROR( "Cannot begin scene - renderer is null");
        return ctx;
    }

    if (m_sceneActive) {
        SCENE_RENDERER_WARN( "beginScene called while scene already active");
        return ctx;
    }

    // Get camera parameters
    float zoom = camera.getZoom();
    const auto& viewport = camera.getViewport();

    // Get interpolated camera offset and center position
    float rawCameraX = 0.0f;
    float rawCameraY = 0.0f;
    Vector2D cameraCenter = camera.getRenderOffset(rawCameraX, rawCameraY, interpolationAlpha);

    // Calculate view dimensions at 1x scale
    float viewWidth = viewport.width / zoom;
    float viewHeight = viewport.height / zoom;

    // Calculate floored camera position for tile alignment
    float flooredCameraX = std::floor(rawCameraX);
    float flooredCameraY = std::floor(rawCameraY);

    // Store render state for endScene
    m_currentZoom = zoom;
    m_viewportWidth = viewWidth;
    m_viewportHeight = viewHeight;

    // Apply zoom directly via SDL_SetRenderScale - NO intermediate texture needed
    // This eliminates all render target switches which cause GPU sync/hitching
    SDL_SetRenderScale(renderer, zoom, zoom);

    m_sceneActive = true;

    // Populate context
    ctx.cameraX = flooredCameraX;
    ctx.cameraY = flooredCameraY;
    ctx.flooredCameraX = flooredCameraX;
    ctx.flooredCameraY = flooredCameraY;
    ctx.viewWidth = viewWidth;
    ctx.viewHeight = viewHeight;
    ctx.zoom = zoom;
    ctx.cameraCenter = cameraCenter;
    ctx.valid = true;

    return ctx;
}

void SceneRenderer::endScene(SDL_Renderer* renderer) {
    if (!m_sceneActive) {
        SCENE_RENDERER_WARN( "endScene called without matching beginScene");
        return;
    }

    if (!renderer) {
        SCENE_RENDERER_ERROR( "Cannot end scene - renderer is null");
        m_sceneActive = false;
        return;
    }

    // Reset render scale to 1.0 for UI rendering
    // No intermediate texture, no render target switches - just reset scale
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    m_sceneActive = false;
}

} // namespace HammerEngine
