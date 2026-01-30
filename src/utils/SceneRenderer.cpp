/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "utils/SceneRenderer.hpp"
#include "utils/Camera.hpp"
#include "utils/FrameProfiler.hpp"
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

    // Allocate for zoom 1.0 (largest view) to prevent reallocation on zoom changes
    // Minimum zoom is 1.0, so current request at any zoom will be <= this size
    int allocWidth = std::max(width * 3, m_textureWidth);   // 3x covers zoom 3→1 transition
    int allocHeight = std::max(height * 3, m_textureHeight);

    // Create texture with headroom for all zoom levels
    SDL_Texture* tex = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        allocWidth,
        allocHeight
    );

    if (!tex) {
        SCENE_RENDERER_ERROR( std::format("Failed to create intermediate texture {}x{}: {}",
                               width, height, SDL_GetError()));
        return false;
    }

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);  // Pixel-perfect, no bilinear filtering

    m_intermediateTexture = std::shared_ptr<SDL_Texture>(tex, SDL_DestroyTexture);
    m_textureWidth = allocWidth;
    m_textureHeight = allocHeight;

    SCENE_RENDERER_DEBUG(std::format("Created intermediate texture {}x{} (requested {}x{})",
                                      allocWidth, allocHeight, width, height));
    return true;
}

SceneRenderer::SceneContext SceneRenderer::beginScene(
    SDL_Renderer* renderer,
    Camera& camera,
    float interpolationAlpha
) {
    PROFILE_RENDER_GPU(HammerEngine::RenderPhase::BeginScene, renderer);

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

    // Calculate sub-pixel offset for smooth camera scrolling.
    // This represents the fractional pixel position lost when flooring for tile alignment.
    // Applied as negative offset during composite to smoothly shift the entire scene.
    m_subPixelOffsetX = rawCameraX - flooredCameraX;
    m_subPixelOffsetY = rawCameraY - flooredCameraY;

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

    m_sceneActive = true;

    // Populate context
    // ALL rendering uses FLOORED camera for consistent positioning in intermediate texture.
    // Sub-pixel smoothness is achieved via the composite offset in endScene(), not per-entity math.
    // This ensures tiles and entities move together without relative jitter.
    ctx.cameraX = flooredCameraX;        // For entities (floored - sub-pixel via composite)
    ctx.cameraY = flooredCameraY;        // For entities (floored - sub-pixel via composite)
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
    PROFILE_RENDER_GPU(HammerEngine::RenderPhase::EndScene, renderer);

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

    // Use LINEAR filtering for final composite to enable smooth sub-pixel scrolling.
    // This allows fractional pixel offsets without shimmer artifacts.
    // Tiles remain pixel-perfect in the intermediate texture; only the composite is filtered.
    SDL_SetTextureScaleMode(m_intermediateTexture.get(), SDL_SCALEMODE_LINEAR);

    // Composite intermediate texture to screen with sub-pixel offset.
    // Negative offset shifts the scene to compensate for floored tile positions,
    // creating smooth continuous scrolling as camera moves between pixel boundaries.
    SDL_FRect srcRect = {0, 0, m_viewportWidth, m_viewportHeight};
    SDL_FRect destRect = {-m_subPixelOffsetX, -m_subPixelOffsetY, m_viewportWidth, m_viewportHeight};
    SDL_RenderTexture(renderer, m_intermediateTexture.get(), &srcRect, &destRect);

    // Restore NEAREST filtering for next frame's tile rendering
    SDL_SetTextureScaleMode(m_intermediateTexture.get(), SDL_SCALEMODE_NEAREST);

    // Reset render scale to 1.0 for UI rendering
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    m_sceneActive = false;
}

} // namespace HammerEngine
