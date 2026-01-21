/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef SCENE_RENDERER_HPP
#define SCENE_RENDERER_HPP

#include "utils/Vector2D.hpp"
#include <memory>

struct SDL_Renderer;
struct SDL_Texture;

namespace HammerEngine {

class Camera;

/**
 * @brief Utility class for pixel-perfect zoomed scene rendering
 *
 * Owns intermediate render texture for smooth sub-pixel scrolling and zoom.
 * GameStates own an instance (not singleton) following the Camera pattern.
 *
 * Render flow:
 *   ctx = sceneRenderer.beginScene(renderer, camera, alpha)
 *   worldMgr.render(renderer, ctx.flooredCameraX, ctx.flooredCameraY, ...)
 *   entities.render(renderer, ctx.cameraX, ctx.cameraY, ...)
 *   sceneRenderer.endScene(renderer)
 *   ui.render(renderer)  // At 1.0 scale (endScene resets render scale)
 */
class SceneRenderer {
public:
    /**
     * @brief Context returned by beginScene() containing all render parameters
     *
     * Provides both raw camera position (for entities) and floored position
     * (for tiles). This separation allows pixel-perfect tile alignment while
     * permitting smooth sub-pixel entity movement.
     */
    struct SceneContext {
        // Raw camera position (for entities that need sub-pixel positioning)
        float cameraX{0.0f};
        float cameraY{0.0f};

        // Floored camera position (for tiles - integer pixel alignment)
        float flooredCameraX{0.0f};
        float flooredCameraY{0.0f};

        // View dimensions at 1x scale (divide by zoom for effective view)
        float viewWidth{0.0f};
        float viewHeight{0.0f};

        // Current zoom level
        float zoom{1.0f};

        // Camera world position (for followed entity - avoids double-interpolation jitter)
        Vector2D cameraCenter{0.0f, 0.0f};

        // Whether the context is valid (beginScene succeeded)
        bool valid{false};

        explicit operator bool() const { return valid; }
    };

    SceneRenderer();
    ~SceneRenderer();

    // Non-copyable (owns texture resources)
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    // Movable
    SceneRenderer(SceneRenderer&&) noexcept;
    SceneRenderer& operator=(SceneRenderer&&) noexcept;

    /**
     * @brief Begin scene rendering - sets intermediate texture as render target
     *
     * Sets up the intermediate render texture and calculates both raw and floored
     * camera positions. Tiles should use floored positions for pixel-perfect
     * alignment; entities should use raw positions for smooth movement.
     *
     * @param renderer SDL renderer
     * @param camera Camera for position and zoom
     * @param interpolationAlpha Interpolation alpha for smooth rendering
     * @return SceneContext with render parameters, or invalid context on failure
     */
    SceneContext beginScene(SDL_Renderer* renderer, Camera& camera, float interpolationAlpha);

    /**
     * @brief End scene rendering - composite to screen with zoom and sub-pixel offset
     *
     * Composites the intermediate texture to the screen applying zoom scaling
     * and sub-pixel offset for smooth scrolling. Resets render scale to 1.0
     * so UI can render at native resolution.
     *
     * @param renderer SDL renderer
     */
    void endScene(SDL_Renderer* renderer);

    /**
     * @brief Check if a scene is currently active (between begin/end)
     * @return True if beginScene was called without matching endScene
     */
    bool isSceneActive() const { return m_sceneActive; }

private:
    // Intermediate render texture for smooth sub-pixel scrolling
    std::shared_ptr<SDL_Texture> m_intermediateTexture;
    int m_textureWidth{0};
    int m_textureHeight{0};

    // Render state
    bool m_sceneActive{false};
    float m_currentZoom{1.0f};
    float m_viewportWidth{0.0f};
    float m_viewportHeight{0.0f};
    float m_subPixelOffsetX{0.0f};
    float m_subPixelOffsetY{0.0f};

    /**
     * @brief Ensure intermediate texture exists with required dimensions
     * @param renderer SDL renderer
     * @param width Required width
     * @param height Required height
     * @return True if texture is ready
     */
    bool ensureTextureSize(SDL_Renderer* renderer, int width, int height);
};

} // namespace HammerEngine

#endif // SCENE_RENDERER_HPP
