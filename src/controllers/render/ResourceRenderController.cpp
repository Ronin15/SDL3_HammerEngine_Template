/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/render/ResourceRenderController.hpp"
#include "managers/EntityDataManager.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <vector>

void ResourceRenderController::update(float deltaTime) {
    updateDroppedItemAnimations(deltaTime);
    updateContainerStates(deltaTime);
    updateHarvestableStates(deltaTime);
}

void ResourceRenderController::updateDroppedItemAnimations(float deltaTime) {
    auto& edm = EntityDataManager::Instance();

    // Single iteration over DroppedItem indices for both bobbing and frame animation
    for (size_t idx : edm.getIndicesByKind(EntityKind::DroppedItem)) {
        const auto& hot = edm.getHotDataByIndex(idx);
        if (hot.tier != SimulationTier::Active) continue;  // Only update Active tier

        auto& r = edm.getItemRenderDataByTypeIndex(hot.typeLocalIndex);

        // Update bob phase (sine wave)
        r.bobPhase += BOB_SPEED * deltaTime;
        if (r.bobPhase >= TWO_PI) {
            r.bobPhase -= TWO_PI;
        }

        // Update frame animation (if multi-frame)
        if (r.numFrames > 1) {
            float speed = static_cast<float>(r.animSpeedMs) * 0.001f;
            r.animTimer += deltaTime;
            if (r.animTimer >= speed) {
                r.currentFrame = (r.currentFrame + 1) % r.numFrames;
                r.animTimer -= speed;
            }
        }
    }
}

void ResourceRenderController::updateContainerStates([[maybe_unused]] float deltaTime) {
    // Container open/close animations will be implemented when containers are added
    // For now, containers are static (just open or closed state)
}

void ResourceRenderController::updateHarvestableStates([[maybe_unused]] float deltaTime) {
    // Harvestable animations (e.g., swaying trees) will be implemented later
    // For now, harvestables are static
}

void ResourceRenderController::renderDroppedItems(SDL_Renderer* renderer, float cameraX, float cameraY, float alpha) {
    auto& edm = EntityDataManager::Instance();

    // Use getIndicesByKind for efficient filtered iteration
    for (size_t idx : edm.getIndicesByKind(EntityKind::DroppedItem)) {
        const auto& hot = edm.getHotDataByIndex(idx);
        if (hot.tier != SimulationTier::Active) continue;  // Only render Active tier

        const auto& r = edm.getItemRenderDataByTypeIndex(hot.typeLocalIndex);

        // Skip if no texture
        if (!r.cachedTexture) continue;

        // Interpolate position
        float interpX = hot.transform.previousPosition.getX() +
            (hot.transform.position.getX() - hot.transform.previousPosition.getX()) * alpha;
        float interpY = hot.transform.previousPosition.getY() +
            (hot.transform.position.getY() - hot.transform.previousPosition.getY()) * alpha;

        // Add bobbing offset
        float bobOffset = std::sin(r.bobPhase) * r.bobAmplitude;

        // Source rect from pre-calculated atlas coords
        SDL_FRect srcRect = {
            static_cast<float>(r.atlasX + r.currentFrame * r.frameWidth),
            static_cast<float>(r.atlasY),
            static_cast<float>(r.frameWidth),
            static_cast<float>(r.frameHeight)
        };

        // Calculate destination rect (centered on position, with bob)
        float halfW = static_cast<float>(r.frameWidth) * 0.5f;
        float halfH = static_cast<float>(r.frameHeight) * 0.5f;
        SDL_FRect destRect = {
            interpX - cameraX - halfW,
            interpY - cameraY - halfH + bobOffset,
            static_cast<float>(r.frameWidth),
            static_cast<float>(r.frameHeight)
        };

        SDL_RenderTexture(renderer, r.cachedTexture, &srcRect, &destRect);
    }
}

void ResourceRenderController::renderContainers(SDL_Renderer* renderer, float cameraX, float cameraY, float alpha) {
    auto& edm = EntityDataManager::Instance();

    // Use getIndicesByKind for efficient filtered iteration
    for (size_t idx : edm.getIndicesByKind(EntityKind::Container)) {
        const auto& hot = edm.getHotDataByIndex(idx);
        if (hot.tier != SimulationTier::Active) continue;  // Only render Active tier

        const auto& containerData = edm.getContainerData(hot.typeLocalIndex);
        const auto& r = edm.getContainerRenderDataByTypeIndex(hot.typeLocalIndex);

        // Choose texture based on open/closed state
        SDL_Texture* texture = containerData.isOpen() ? r.openTexture : r.closedTexture;
        if (!texture) continue;

        // Interpolate position
        float interpX = hot.transform.previousPosition.getX() +
            (hot.transform.position.getX() - hot.transform.previousPosition.getX()) * alpha;
        float interpY = hot.transform.previousPosition.getY() +
            (hot.transform.position.getY() - hot.transform.previousPosition.getY()) * alpha;

        // Calculate source rect
        SDL_FRect srcRect = {
            static_cast<float>(r.currentFrame * r.frameWidth),
            0.0f,
            static_cast<float>(r.frameWidth),
            static_cast<float>(r.frameHeight)
        };

        // Calculate destination rect (centered)
        float halfW = static_cast<float>(r.frameWidth) * 0.5f;
        float halfH = static_cast<float>(r.frameHeight) * 0.5f;
        SDL_FRect destRect = {
            interpX - cameraX - halfW,
            interpY - cameraY - halfH,
            static_cast<float>(r.frameWidth),
            static_cast<float>(r.frameHeight)
        };

        SDL_RenderTexture(renderer, texture, &srcRect, &destRect);
    }
}

void ResourceRenderController::renderHarvestables(SDL_Renderer* renderer, float cameraX, float cameraY, float alpha) {
    auto& edm = EntityDataManager::Instance();

    // Use getIndicesByKind for efficient filtered iteration
    for (size_t idx : edm.getIndicesByKind(EntityKind::Harvestable)) {
        const auto& hot = edm.getHotDataByIndex(idx);
        if (hot.tier != SimulationTier::Active) continue;  // Only render Active tier

        const auto& harvData = edm.getHarvestableData(hot.typeLocalIndex);
        const auto& r = edm.getHarvestableRenderDataByTypeIndex(hot.typeLocalIndex);

        // Choose texture based on depleted state
        SDL_Texture* texture = harvData.isDepleted ? r.depletedTexture : r.normalTexture;
        if (!texture) texture = r.normalTexture;  // Fallback to normal if no depleted texture
        if (!texture) continue;

        // Interpolate position
        float interpX = hot.transform.previousPosition.getX() +
            (hot.transform.position.getX() - hot.transform.previousPosition.getX()) * alpha;
        float interpY = hot.transform.previousPosition.getY() +
            (hot.transform.position.getY() - hot.transform.previousPosition.getY()) * alpha;

        // Calculate source rect
        SDL_FRect srcRect = {
            static_cast<float>(r.currentFrame * r.frameWidth),
            0.0f,
            static_cast<float>(r.frameWidth),
            static_cast<float>(r.frameHeight)
        };

        // Calculate destination rect (centered)
        float halfW = static_cast<float>(r.frameWidth) * 0.5f;
        float halfH = static_cast<float>(r.frameHeight) * 0.5f;
        SDL_FRect destRect = {
            interpX - cameraX - halfW,
            interpY - cameraY - halfH,
            static_cast<float>(r.frameWidth),
            static_cast<float>(r.frameHeight)
        };

        SDL_RenderTexture(renderer, texture, &srcRect, &destRect);
    }
}

void ResourceRenderController::clearAll() {
    auto& edm = EntityDataManager::Instance();

    // Copy indices to local buffer to avoid span invalidation during iteration
    // (getIndicesByKind returns a span that could be invalidated if kind indices are rebuilt)
    std::vector<size_t> toDestroy;

    // Collect dropped item indices
    for (size_t idx : edm.getIndicesByKind(EntityKind::DroppedItem)) {
        toDestroy.push_back(idx);
    }

    // Collect container indices
    for (size_t idx : edm.getIndicesByKind(EntityKind::Container)) {
        toDestroy.push_back(idx);
    }

    // Collect harvestable indices
    for (size_t idx : edm.getIndicesByKind(EntityKind::Harvestable)) {
        toDestroy.push_back(idx);
    }

    // Now destroy all collected entities
    for (size_t idx : toDestroy) {
        EntityHandle handle = edm.getHandle(idx);
        if (handle.isValid()) {
            edm.destroyEntity(handle);
        }
    }
}
