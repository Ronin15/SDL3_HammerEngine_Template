/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/render/NPCRenderController.hpp"
#include "entities/EntityHandle.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/AIManager.hpp"
#include <SDL3/SDL.h>
#include <cmath>

void NPCRenderController::update(float deltaTime) {
    auto& edm = EntityDataManager::Instance();

    // Only update Active tier NPCs (same as AIManager)
    for (size_t idx : edm.getActiveIndices()) {
        const auto& hot = edm.getHotDataByIndex(idx);
        if (hot.kind != EntityKind::NPC) continue;

        auto& r = edm.getNPCRenderDataByTypeIndex(hot.typeLocalIndex);

        // Velocity check once - sets row, frames, speed, flip
        float vx = hot.transform.velocity.getX();
        float vy = hot.transform.velocity.getY();
        bool moving = (vx * vx + vy * vy) > MOVEMENT_THRESHOLD_SQ;

        r.currentRow = moving ? r.moveRow : r.idleRow;
        uint8_t frames = moving ? r.numMoveFrames : r.numIdleFrames;
        float speed = static_cast<float>(moving ? r.moveSpeedMs : r.idleSpeedMs) * 0.001f;

        r.animationAccumulator += deltaTime;
        if (r.animationAccumulator >= speed) {
            r.currentFrame = (r.currentFrame + 1) % frames;
            r.animationAccumulator -= speed;
        }

        if (vx < 0.0f) r.flipMode = static_cast<uint8_t>(SDL_FLIP_HORIZONTAL);
        else if (vx > 0.0f) r.flipMode = static_cast<uint8_t>(SDL_FLIP_NONE);
    }
}

void NPCRenderController::renderNPCs(SDL_Renderer* renderer, float cameraX, float cameraY, float alpha) {
    auto& edm = EntityDataManager::Instance();

    // Only render Active tier NPCs (same as AIManager)
    for (size_t idx : edm.getActiveIndices()) {
        const auto& hot = edm.getHotDataByIndex(idx);
        if (hot.kind != EntityKind::NPC) continue;

        const auto& r = edm.getNPCRenderDataByTypeIndex(hot.typeLocalIndex);

        // Interpolate position
        float interpX = hot.transform.previousPosition.getX() +
            (hot.transform.position.getX() - hot.transform.previousPosition.getX()) * alpha;
        float interpY = hot.transform.previousPosition.getY() +
            (hot.transform.position.getY() - hot.transform.previousPosition.getY()) * alpha;

        // All render state set by update() - just read and draw
        // Add atlas offset to source rect for atlas-based rendering
        SDL_FRect srcRect = {
            static_cast<float>(r.atlasX + r.currentFrame * r.frameWidth),
            static_cast<float>(r.atlasY + r.currentRow * r.frameHeight),
            static_cast<float>(r.frameWidth),
            static_cast<float>(r.frameHeight)
        };

        float halfW = static_cast<float>(r.frameWidth) * 0.5f;
        float halfH = static_cast<float>(r.frameHeight) * 0.5f;
        // Pixel snap to prevent shimmer during diagonal camera movement
        SDL_FRect destRect = {
            std::floor(interpX - cameraX - halfW),
            std::floor(interpY - cameraY - halfH),
            static_cast<float>(r.frameWidth),
            static_cast<float>(r.frameHeight)
        };

        SDL_RenderTextureRotated(renderer, r.cachedTexture, &srcRect, &destRect,
                                  0.0, nullptr, static_cast<SDL_FlipMode>(r.flipMode));
    }
}

void NPCRenderController::clearSpawnedNPCs() {
    auto& edm = EntityDataManager::Instance();
    auto& aiMgr = AIManager::Instance();

    // Get all NPC indices
    auto npcIndices = edm.getIndicesByKind(EntityKind::NPC);

    // Unregister from AI and destroy via EDM
    for (size_t idx : npcIndices) {
        EntityHandle handle = edm.getHandle(idx);
        if (handle.isValid()) {
            aiMgr.unregisterEntity(handle);
            edm.destroyEntity(handle);
        }
    }
}
