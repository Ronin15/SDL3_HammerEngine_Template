/* Copyright (c) 2025 Hammer Forged Games
 * Licensed under the MIT License */

#ifndef GPU_SCENE_RECORDER_HPP
#define GPU_SCENE_RECORDER_HPP

#include "utils/Vector2D.hpp"

// Forward declaration of SDL GPU types (must be outside namespace)
struct SDL_GPURenderPass;

namespace HammerEngine {

class Camera;
class GPURenderer;
class SpriteBatch;

/**
 * @brief Context returned by beginRecording() containing all render parameters
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

    // Whether the context is valid (beginRecording succeeded)
    bool valid{false};

    explicit operator bool() const { return valid; }
};

/**
 * @brief GPU scene recording coordinator for state-owned draw preparation
 *
 * Coordinates state-side GPU recording before the engine opens the SDL scene pass.
 * Owns sprite batch begin/end lifecycle, integrates with FrameProfiler,
 * and provides GPUSceneContext for systems to record draw data.
 *
 * ARCHITECTURE:
 * - GPUSceneRecorder owns batch lifecycle (begin/end)
 * - Systems (WorldManager, NPCRenderController, etc.) just call draw()
 * - Sub-pixel camera smoothness handled by composite shader params
 * - Profiler integration via PROFILE_RENDER_GPU macros
 * - Engine-owned SDL pass lifetime stays in GameEngine/GPURenderer
 *
 * Render flow:
 *   auto ctx = gpuSceneRecorder.beginRecording(gpuRenderer, camera, alpha);
 *   if (ctx) {
 *       worldMgr.recordGPUTiles(ctx);     // calls ctx.spriteBatch->draw()
 *       npcCtrl.recordGPU(ctx);           // calls ctx.spriteBatch->draw()
 *       resourceCtrl.recordGPU(ctx);      // calls ctx.spriteBatch->draw()
 *       gpuSceneRecorder.endSpriteBatch();
 *
 *       player->recordGPUVertices(...);   // entity batch (separate texture)
 *       particleMgr.recordGPUVertices(...); // particle pool
 *   }
 *   gpuSceneRecorder.endRecording();
 */
class GPUSceneRecorder {
public:
    GPUSceneRecorder();
    ~GPUSceneRecorder();

    // Non-copyable (owns rendering state)
    GPUSceneRecorder(const GPUSceneRecorder&) = delete;
    GPUSceneRecorder& operator=(const GPUSceneRecorder&) = delete;

    // Movable
    GPUSceneRecorder(GPUSceneRecorder&&) noexcept;
    GPUSceneRecorder& operator=(GPUSceneRecorder&&) noexcept;

    /**
     * @brief Begin scene-data recording for the engine-owned scene pass
     *
     * Sets up the sprite batch with atlas texture and calculates floored camera
     * position. All atlas-based content should use the returned context's
     * spriteBatch->draw() method.
     *
     * @param gpuRenderer GPU renderer instance
     * @param camera Camera for position and zoom
     * @param interpolationAlpha Interpolation alpha for smooth rendering
     * @return GPUSceneContext with recording parameters, or invalid context on failure
     */
    GPUSceneContext beginRecording(GPURenderer& gpuRenderer, Camera& camera, float interpolationAlpha);

    /**
     * @brief End sprite batch recording - finalizes atlas-based sprites
     *
     * Call this after all atlas-based systems have drawn (world, NPCs, resources)
     * but before entity batch users (player) start recording.
     */
    void endSpriteBatch();

    /**
     * @brief End scene-data recording and clear recorder state
     *
     * Called at the end of recordGPUVertices to finalize state-side recording.
     */
    void endRecording();

    /**
     * @brief Render previously recorded scene data into the active scene pass
     *
     * Called during renderGPUScene to issue draw calls into the engine-owned pass.
     *
     * @param gpuRenderer GPU renderer instance
     * @param scenePass Active scene render pass
     */
    void renderRecordedScene(GPURenderer& gpuRenderer, SDL_GPURenderPass* scenePass);

    /**
     * @brief Check if scene-data recording is currently active
     */
    bool isRecordingActive() const { return m_recordingActive; }

private:
    // Recording state
    bool m_recordingActive{false};
    bool m_spriteBatchActive{false};

    // Cached references for render phase
    GPURenderer* m_gpuRenderer{nullptr};
    SpriteBatch* m_spriteBatch{nullptr};

    // Cached scene params for render phase
    float m_zoom{1.0f};
};

} // namespace HammerEngine

#endif // GPU_SCENE_RECORDER_HPP
