/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef WORLD_RENDER_PIPELINE_HPP
#define WORLD_RENDER_PIPELINE_HPP

#include "utils/SceneRenderer.hpp"
#include "utils/Vector2D.hpp"
#include <memory>

struct SDL_Renderer;

namespace HammerEngine {

class Camera;

/**
 * @brief Unified facade for world rendering that coordinates chunk management and scene composition
 *
 * WorldRenderPipeline eliminates hitching when camera moves by providing:
 * - Predictive prefetching: Tracks camera velocity and prefetches chunks in movement direction
 * - Unified coordination: Single point of control for TileRenderer and SceneRenderer
 * - Loading-time pre-warm: Renders visible chunks during loading screen
 * - Dynamic render budget: Renders more chunks when camera moving fast
 *
 * ARCHITECTURE:
 * This class is a facade that owns the SceneRenderer and coordinates with WorldManager's
 * TileRenderer. It computes render parameters once per frame and shares them across
 * all rendering operations.
 *
 * Usage in GameState:
 *   void update(float dt) {
 *       updateCamera(dt);
 *       m_renderPipeline->prepareChunks(renderer, *m_camera, dt);  // Prefetch
 *   }
 *
 *   void render(SDL_Renderer* renderer, float interpolation) {
 *       auto ctx = m_renderPipeline->beginScene(renderer, *m_camera, interpolation);
 *       if (ctx.valid) {
 *           m_renderPipeline->renderWorld(renderer, ctx);
 *           // Render entities using ctx.cameraX, ctx.cameraY
 *       }
 *       m_renderPipeline->endScene(renderer);
 *       ui.render(renderer);
 *   }
 */
class WorldRenderPipeline {
public:
    /**
     * @brief Render context containing all parameters needed for a frame
     *
     * Computed once in beginScene() and reused across all rendering operations.
     * Eliminates redundant camera calculations.
     */
    struct RenderContext {
        // Camera position for entities (floored - sub-pixel via composite offset)
        float cameraX{0.0f};
        float cameraY{0.0f};

        // Camera position for tiles (floored - pixel-aligned, same as cameraX/Y)
        float flooredCameraX{0.0f};
        float flooredCameraY{0.0f};

        // Sub-pixel offset for smooth scrolling (applied in endScene)
        float subPixelOffsetX{0.0f};
        float subPixelOffsetY{0.0f};

        // View dimensions at 1x scale (divide by zoom for effective view)
        float viewWidth{0.0f};
        float viewHeight{0.0f};

        // Current zoom level
        float zoom{1.0f};

        // Camera velocity for external use (e.g., particle systems)
        float velocityX{0.0f};
        float velocityY{0.0f};

        // Camera world position (for followed entity - avoids double-interpolation jitter)
        Vector2D cameraCenter{0.0f, 0.0f};

        // Whether the context is valid (beginScene succeeded)
        bool valid{false};

        explicit operator bool() const { return valid; }
    };

    WorldRenderPipeline();
    ~WorldRenderPipeline();

    // Non-copyable (owns resources)
    WorldRenderPipeline(const WorldRenderPipeline&) = delete;
    WorldRenderPipeline& operator=(const WorldRenderPipeline&) = delete;

    // Movable
    WorldRenderPipeline(WorldRenderPipeline&&) noexcept;
    WorldRenderPipeline& operator=(WorldRenderPipeline&&) noexcept;

    /**
     * @brief Phase 1: Prepare chunks (call in update, before render)
     *
     * Tracks camera velocity and prefetches chunks in the direction of movement.
     * This is predictive - chunks are prepared before they enter the viewport.
     * Gets renderer from WorldManager (must be set via WorldManager::setRenderer).
     *
     * @param camera Camera for position and velocity tracking
     * @param deltaTime Time since last update for velocity calculation
     */
    void prepareChunks(Camera& camera, float deltaTime);

    /**
     * @brief Phase 2: Begin scene rendering
     *
     * Sets up the SceneRenderer intermediate texture and calculates all render
     * parameters once. Returns a context to be passed to renderWorld() and
     * used for entity rendering.
     *
     * @param renderer SDL renderer
     * @param camera Camera for position and zoom
     * @param interpolationAlpha Interpolation alpha for smooth rendering
     * @return RenderContext with all render parameters, or invalid context on failure
     */
    RenderContext beginScene(SDL_Renderer* renderer, Camera& camera, float interpolationAlpha);

    /**
     * @brief Phase 3: Render world tiles
     *
     * Renders visible tile chunks to the current render target using the
     * pre-computed context. Call this after beginScene() and before entity rendering.
     *
     * @param renderer SDL renderer
     * @param ctx RenderContext from beginScene()
     */
    void renderWorld(SDL_Renderer* renderer, const RenderContext& ctx);

    /**
     * @brief Phase 4: End scene rendering
     *
     * Composites the intermediate texture to screen with zoom and sub-pixel offset.
     * Resets render scale to 1.0 for UI rendering.
     *
     * @param renderer SDL renderer
     */
    void endScene(SDL_Renderer* renderer);

    /**
     * @brief Pre-warm visible chunks during loading screen
     *
     * Renders all chunks that would be visible at the given position.
     * Call this after world generation to eliminate hitches on initial movement.
     *
     * @param renderer SDL renderer
     * @param centerX World X coordinate of initial view center
     * @param centerY World Y coordinate of initial view center
     * @param viewWidth Viewport width
     * @param viewHeight Viewport height
     */
    void prewarmVisibleChunks(SDL_Renderer* renderer, float centerX, float centerY,
                              float viewWidth, float viewHeight);

    /**
     * @brief Get the underlying SceneRenderer (for advanced use cases)
     * @return Pointer to SceneRenderer, or nullptr if not initialized
     */
    SceneRenderer* getSceneRenderer() { return m_sceneRenderer.get(); }

    /**
     * @brief Check if a scene is currently active
     * @return True if between beginScene/endScene
     */
    bool isSceneActive() const;

    /**
     * @brief Get current camera velocity
     * @return Camera velocity vector
     */
    const Vector2D& getCameraVelocity() const { return m_cameraVelocity; }

    /**
     * @brief Get camera speed magnitude
     * @return Speed in pixels per second
     */
    float getCameraSpeed() const;

private:
    // Scene rendering
    std::unique_ptr<SceneRenderer> m_sceneRenderer;

    // Velocity tracking for predictive prefetching
    Vector2D m_lastCameraPos{0.0f, 0.0f};
    Vector2D m_cameraVelocity{0.0f, 0.0f};
    bool m_hasLastPosition{false};

    // Prefetch configuration
    static constexpr int PREFETCH_MARGIN_CHUNKS = 3;
    static constexpr float FAST_CAMERA_THRESHOLD = 200.0f;  // pixels/second
    static constexpr float VELOCITY_SMOOTHING = 0.5f;  // Higher = more responsive to direction changes
};

} // namespace HammerEngine

#endif // WORLD_RENDER_PIPELINE_HPP
