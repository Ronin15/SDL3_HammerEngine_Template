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

// Extra padding for intermediate texture to allow for sprite overhang
static constexpr int TEXTURE_PADDING = 128;

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

    // With NEAREST scalemode, sub-pixel offsets cause shifting artifacts.
    // Use integer-only camera positioning for consistent pixel-perfect rendering.
    m_subPixelOffsetX = 0.0f;
    m_subPixelOffsetY = 0.0f;

    // Store render state for endScene
    m_currentZoom = zoom;
    m_viewportWidth = viewWidth;
    m_viewportHeight = viewHeight;

    // Ensure intermediate texture is large enough
    int requiredWidth = static_cast<int>(viewWidth) + TEXTURE_PADDING;
    int requiredHeight = static_cast<int>(viewHeight) + TEXTURE_PADDING;

    if (!ensureTextureSize(renderer, requiredWidth, requiredHeight)) {
        return ctx;
    }

    // Set render target to intermediate texture
    SDL_SetRenderTarget(renderer, m_intermediateTexture.get());
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    m_sceneActive = true;

    // Populate context
    // Tiles use FLOORED camera for pixel-perfect grid alignment.
    // Entities use RAW camera so interpolated positions stay consistent relative to camera center.
    // This prevents entity oscillation when camera crosses pixel boundaries.
    ctx.cameraX = rawCameraX;           // For entities (smooth)
    ctx.cameraY = rawCameraY;           // For entities (smooth)
    ctx.flooredCameraX = flooredCameraX; // For tiles (pixel-aligned)
    ctx.flooredCameraY = flooredCameraY; // For tiles (pixel-aligned)
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

    // Restore screen as render target
    SDL_SetRenderTarget(renderer, nullptr);

    // Apply zoom via render scale
    SDL_SetRenderScale(renderer, m_currentZoom, m_currentZoom);

    // Composite intermediate texture to screen with sub-pixel offset
    SDL_FRect srcRect = {0, 0, m_viewportWidth, m_viewportHeight};
    SDL_FRect destRect = {m_subPixelOffsetX, m_subPixelOffsetY, m_viewportWidth, m_viewportHeight};
    SDL_RenderTexture(renderer, m_intermediateTexture.get(), &srcRect, &destRect);

    // Reset render scale to 1.0 for UI rendering
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    m_sceneActive = false;
}

} // namespace HammerEngine
