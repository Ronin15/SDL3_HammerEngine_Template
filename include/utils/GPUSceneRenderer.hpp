/* Copyright (c) 2025 Hammer Forged Games
 * Licensed under the MIT License */

#ifndef GPU_SCENE_RENDERER_HPP
#define GPU_SCENE_RENDERER_HPP

#ifdef USE_SDL3_GPU

#include "utils/Vector2D.hpp"

// Forward declaration of SDL GPU types (must be outside namespace)
struct SDL_GPURenderPass;

namespace HammerEngine {

class Camera;
class GPURenderer;
class SpriteBatch;

/**
 * @brief Context returned by beginScene() containing all render parameters
 *
 * Systems use this context to draw to the sprite batch without managing
 * batch lifecycle. Camera coordinates are floored for pixel alignment;
 * sub-pixel smoothness comes from the composite shader.
 */
struct GPUSceneContext {
    // Floored camera position for pixel-aligned rendering
    float cameraX{0.0f};
    float cameraY{0.0f};

    // View dimensions at 1x scale
    float viewWidth{0.0f};
    float viewHeight{0.0f};

    // Current zoom level
    float zoom{1.0f};

    // Interpolation alpha for smooth rendering
    float interpolationAlpha{1.0f};

    // Camera world position (for spatial queries)
    Vector2D cameraCenter{0.0f, 0.0f};

    // Sprite batch for atlas-based drawing (world tiles, NPCs, resources)
    // Systems call spriteBatch->draw() - no begin/end management needed
    SpriteBatch* spriteBatch{nullptr};

    // Whether the context is valid (beginScene succeeded)
    bool valid{false};

    explicit operator bool() const { return valid; }
};

/**
 * @brief GPU scene rendering coordinator - facade for GPU rendering pipeline
 *
 * Mirrors SceneRenderer (SDL_Renderer path) for GPU rendering coordination.
 * Owns sprite batch begin/end lifecycle, integrates with FrameProfiler,
 * and provides GPUSceneContext for systems to draw.
 *
 * ARCHITECTURE:
 * - GPUSceneRenderer owns batch lifecycle (begin/end)
 * - Systems (WorldManager, NPCRenderController, etc.) just call draw()
 * - Sub-pixel camera smoothness handled by composite shader params
 * - Profiler integration via PROFILE_RENDER_GPU macros
 *
 * Render flow:
 *   auto ctx = gpuSceneRenderer.beginScene(gpuRenderer, camera, alpha);
 *   if (ctx) {
 *       worldMgr.recordGPUTiles(ctx);     // calls ctx.spriteBatch->draw()
 *       npcCtrl.recordGPU(ctx);           // calls ctx.spriteBatch->draw()
 *       resourceCtrl.recordGPU(ctx);      // calls ctx.spriteBatch->draw()
 *       gpuSceneRenderer.endSpriteBatch();
 *
 *       player->recordGPUVertices(...);   // entity batch (separate texture)
 *       particleMgr.recordGPUVertices(...); // particle pool
 *   }
 *   gpuSceneRenderer.endScene();
 */
class GPUSceneRenderer {
public:
    GPUSceneRenderer();
    ~GPUSceneRenderer();

    // Non-copyable (owns rendering state)
    GPUSceneRenderer(const GPUSceneRenderer&) = delete;
    GPUSceneRenderer& operator=(const GPUSceneRenderer&) = delete;

    // Movable
    GPUSceneRenderer(GPUSceneRenderer&&) noexcept;
    GPUSceneRenderer& operator=(GPUSceneRenderer&&) noexcept;

    /**
     * @brief Begin scene rendering - sets up sprite batch and calculates camera params
     *
     * Sets up the sprite batch with atlas texture and calculates floored camera
     * position. All atlas-based content should use the returned context's
     * spriteBatch->draw() method.
     *
     * @param gpuRenderer GPU renderer instance
     * @param camera Camera for position and zoom
     * @param interpolationAlpha Interpolation alpha for smooth rendering
     * @return GPUSceneContext with render parameters, or invalid context on failure
     */
    GPUSceneContext beginScene(GPURenderer& gpuRenderer, Camera& camera, float interpolationAlpha);

    /**
     * @brief End sprite batch recording - finalizes atlas-based sprites
     *
     * Call this after all atlas-based systems have drawn (world, NPCs, resources)
     * but before entity batch users (player) start recording.
     */
    void endSpriteBatch();

    /**
     * @brief End scene - cleanup and finalize
     *
     * Called at the end of recordGPUVertices to finalize scene recording state.
     */
    void endScene();

    /**
     * @brief Render the scene pass - issues draw calls for recorded sprites
     *
     * Called during renderGPUScene to issue the actual draw calls.
     *
     * @param gpuRenderer GPU renderer instance
     * @param scenePass Active scene render pass
     */
    void renderScene(GPURenderer& gpuRenderer, SDL_GPURenderPass* scenePass);

    /**
     * @brief Check if a scene is currently active (between beginScene/endScene)
     */
    bool isSceneActive() const { return m_sceneActive; }

private:
    // Scene state
    bool m_sceneActive{false};
    bool m_spriteBatchActive{false};

    // Cached references for render phase
    GPURenderer* m_gpuRenderer{nullptr};
    SpriteBatch* m_spriteBatch{nullptr};

    // Cached scene params for render phase
    float m_zoom{1.0f};
};

} // namespace HammerEngine

#endif // USE_SDL3_GPU

#endif // GPU_SCENE_RENDERER_HPP
