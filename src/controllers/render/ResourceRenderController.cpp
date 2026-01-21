/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/render/ResourceRenderController.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include "utils/Camera.hpp"
#include "utils/Vector2D.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <vector>

void ResourceRenderController::update(float deltaTime, const HammerEngine::Camera& camera) {
    updateDroppedItemAnimations(deltaTime, camera);
    updateContainerStates(deltaTime, camera);
    updateHarvestableStates(deltaTime, camera);
}

void ResourceRenderController::updateDroppedItemAnimations(float deltaTime, const HammerEngine::Camera& camera) {
    auto& edm = EntityDataManager::Instance();
    auto& wrm = WorldResourceManager::Instance();

    // Query items in camera view + buffer (not all items in world)
    Vector2D cameraCenter = camera.getPosition();
    const auto& viewport = camera.getViewport();
    float animationRadius = std::sqrt(viewport.width * viewport.width +
                                      viewport.height * viewport.height) * 0.5f + ANIMATION_BUFFER;

    m_visibleItemIndices.clear();
    wrm.queryDroppedItemsInRadius(cameraCenter, animationRadius, m_visibleItemIndices);

    for (size_t idx : m_visibleItemIndices) {
        const auto& hot = edm.getStaticHotDataByIndex(idx);  // Static pool accessor
        if (!hot.isAlive()) continue;

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

void ResourceRenderController::updateContainerStates([[maybe_unused]] float deltaTime,
                                                      [[maybe_unused]] const HammerEngine::Camera& camera) {
    // Container open/close animations will be implemented when containers are added
    // For now, containers are static (just open or closed state)
}

void ResourceRenderController::updateHarvestableStates([[maybe_unused]] float deltaTime,
                                                        [[maybe_unused]] const HammerEngine::Camera& camera) {
    // Harvestable animations (e.g., swaying trees) will be implemented later
    // For now, harvestables are static
}

void ResourceRenderController::renderDroppedItems(SDL_Renderer* renderer, const HammerEngine::Camera& camera,
                                                   float cameraX, float cameraY, float alpha) {
    auto& edm = EntityDataManager::Instance();
    auto& wrm = WorldResourceManager::Instance();

    // Get visibility info from Camera for spatial query (culling tolerance is fine)
    Vector2D cameraCenter = camera.getPosition();
    const auto& viewport = camera.getViewport();
    float visibleRadius = std::sqrt(viewport.width * viewport.width +
                                    viewport.height * viewport.height) * 0.5f;

    // Query only visible items - O(k) where k = cells in view
    m_visibleItemIndices.clear();
    wrm.queryDroppedItemsInRadius(cameraCenter, visibleRadius, m_visibleItemIndices);

    // Use passed cameraX/cameraY (interpolated) for actual rendering

    for (size_t idx : m_visibleItemIndices) {
        const auto& hot = edm.getStaticHotDataByIndex(idx);  // Static pool accessor
        if (!hot.isAlive()) continue;

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
        // Sub-pixel rendering - unified with Player/Particles for smooth diagonal movement
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

void ResourceRenderController::renderContainers(SDL_Renderer* renderer, const HammerEngine::Camera& camera,
                                                 float cameraX, float cameraY, float alpha) {
    auto& edm = EntityDataManager::Instance();
    auto& wrm = WorldResourceManager::Instance();

    // Get visibility info from Camera for spatial query (culling tolerance is fine)
    Vector2D cameraCenter = camera.getPosition();
    const auto& viewport = camera.getViewport();
    float visibleRadius = std::sqrt(viewport.width * viewport.width +
                                    viewport.height * viewport.height) * 0.5f;

    // Query only visible containers - O(k) where k = cells in view
    m_visibleContainerIndices.clear();
    wrm.queryContainersInRadius(cameraCenter, visibleRadius, m_visibleContainerIndices);

    // Use passed cameraX/cameraY (interpolated) for actual rendering

    for (size_t idx : m_visibleContainerIndices) {
        const auto& hot = edm.getStaticHotDataByIndex(idx);  // Static pool accessor
        if (!hot.isAlive()) continue;

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
        // Sub-pixel rendering - unified with Player/Particles for smooth diagonal movement
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

void ResourceRenderController::renderHarvestables(SDL_Renderer* renderer, const HammerEngine::Camera& camera,
                                                   float cameraX, float cameraY, float alpha) {
    auto& edm = EntityDataManager::Instance();
    auto& wrm = WorldResourceManager::Instance();

    // Get visibility info from Camera for spatial query (culling tolerance is fine)
    Vector2D cameraCenter = camera.getPosition();
    const auto& viewport = camera.getViewport();
    float visibleRadius = std::sqrt(viewport.width * viewport.width +
                                    viewport.height * viewport.height) * 0.5f;

    // Query only visible harvestables - O(k) where k = cells in view
    m_visibleHarvestableIndices.clear();
    wrm.queryHarvestablesInRadius(cameraCenter, visibleRadius, m_visibleHarvestableIndices);

    // Use passed cameraX/cameraY (interpolated) for actual rendering

    for (size_t idx : m_visibleHarvestableIndices) {
        const auto& hot = edm.getStaticHotDataByIndex(idx);  // Static pool accessor
        if (!hot.isAlive()) continue;

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
        // Sub-pixel rendering - unified with Player/Particles for smooth diagonal movement
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
    auto& wrm = WorldResourceManager::Instance();

    // Collect all resource indices from all worlds using WRM queries
    // Use large radius to capture all resources in loaded worlds
    std::vector<size_t> toDestroy;
    constexpr float LARGE_RADIUS = 100000.0f;
    Vector2D center(0.0f, 0.0f);

    // Collect dropped items
    wrm.queryDroppedItemsInRadius(center, LARGE_RADIUS, toDestroy);

    // Collect containers
    std::vector<size_t> containers;
    wrm.queryContainersInRadius(center, LARGE_RADIUS, containers);
    toDestroy.insert(toDestroy.end(), containers.begin(), containers.end());

    // Collect harvestables
    std::vector<size_t> harvestables;
    wrm.queryHarvestablesInRadius(center, LARGE_RADIUS, harvestables);
    toDestroy.insert(toDestroy.end(), harvestables.begin(), harvestables.end());

    // Now destroy all collected entities (using static handle accessor)
    for (size_t idx : toDestroy) {
        EntityHandle handle = edm.getStaticHandle(idx);
        if (handle.isValid()) {
            edm.destroyEntity(handle);
        }
    }
}
