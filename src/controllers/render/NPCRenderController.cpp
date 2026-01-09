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
    auto activeIndices = edm.getActiveIndices();

    for (size_t idx : activeIndices) {
        const auto& hotData = edm.getHotDataByIndex(idx);

        // Filter to NPCs only
        if (hotData.kind != EntityKind::NPC || !hotData.isAlive()) {
            continue;
        }

        // Get render data via typeLocalIndex (same index as CharacterData)
        auto& renderData = edm.getNPCRenderDataByTypeIndex(hotData.typeLocalIndex);

        // Skip if no texture cached
        if (!renderData.cachedTexture) {
            continue;
        }

        // AIManager already set velocity via behaviors - just read it
        const auto& transform = hotData.transform;
        float velocityMag = transform.velocity.length();
        bool isMoving = velocityMag > MOVEMENT_THRESHOLD;

        // Select animation parameters based on velocity
        uint8_t targetFrames = isMoving ? renderData.numMoveFrames : renderData.numIdleFrames;
        float speed = static_cast<float>(isMoving ? renderData.moveSpeedMs : renderData.idleSpeedMs) / 1000.0f;
        if (speed <= 0.0f) speed = 0.001f;  // Avoid divide-by-zero
        if (targetFrames == 0) targetFrames = 1;

        // Advance animation frame
        renderData.animationAccumulator += deltaTime;
        if (renderData.animationAccumulator >= speed) {
            renderData.currentFrame = (renderData.currentFrame + 1) % targetFrames;
            renderData.animationAccumulator -= speed;
        }

        // Update flip based on velocity X direction
        if (std::abs(transform.velocity.getX()) > MOVEMENT_THRESHOLD) {
            renderData.flipMode = (transform.velocity.getX() < 0)
                ? static_cast<uint8_t>(SDL_FLIP_HORIZONTAL)
                : static_cast<uint8_t>(SDL_FLIP_NONE);
        }
    }
}

void NPCRenderController::renderNPCs(SDL_Renderer* renderer, float cameraX, float cameraY, float alpha) {
    if (!renderer) {
        return;
    }

    auto& edm = EntityDataManager::Instance();
    auto activeIndices = edm.getActiveIndices();

    for (size_t idx : activeIndices) {
        const auto& hotData = edm.getHotDataByIndex(idx);

        // Filter to NPCs only
        if (hotData.kind != EntityKind::NPC || !hotData.isAlive()) {
            continue;
        }

        // Get render data via typeLocalIndex
        const auto& renderData = edm.getNPCRenderDataByTypeIndex(hotData.typeLocalIndex);
        if (!renderData.cachedTexture) {
            continue;  // Skip if texture not loaded
        }

        // Interpolate position for smooth rendering
        const auto& transform = hotData.transform;
        float interpX = transform.previousPosition.getX() +
            (transform.position.getX() - transform.previousPosition.getX()) * alpha;
        float interpY = transform.previousPosition.getY() +
            (transform.position.getY() - transform.previousPosition.getY()) * alpha;

        // Velocity determines animation row: idle vs moving
        float velocityMag = transform.velocity.length();
        int row = (velocityMag > MOVEMENT_THRESHOLD)
            ? renderData.moveRow
            : renderData.idleRow;

        // Source rect (from sprite sheet)
        SDL_FRect srcRect = {
            static_cast<float>(renderData.currentFrame * renderData.frameWidth),
            static_cast<float>(row * renderData.frameHeight),
            static_cast<float>(renderData.frameWidth),
            static_cast<float>(renderData.frameHeight)
        };

        // Destination rect (screen position, centered on entity)
        SDL_FRect destRect = {
            interpX - cameraX - static_cast<float>(renderData.frameWidth) / 2.0f,
            interpY - cameraY - static_cast<float>(renderData.frameHeight) / 2.0f,
            static_cast<float>(renderData.frameWidth),
            static_cast<float>(renderData.frameHeight)
        };

        // Direct SDL rendering - controller owns render logic
        SDL_RenderTextureRotated(
            renderer,
            renderData.cachedTexture,  // Cached from TextureManager at spawn
            &srcRect,
            &destRect,
            0.0,
            nullptr,
            static_cast<SDL_FlipMode>(renderData.flipMode)
        );
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
