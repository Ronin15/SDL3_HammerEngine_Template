/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef TEXTURE_MANAGER_HPP
#define TEXTURE_MANAGER_HPP

#include <SDL3/SDL.h>
#include <unordered_map>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <vector>

#ifdef USE_SDL3_GPU
#include "gpu/GPUTexture.hpp"
#include <SDL3/SDL_gpu.h>
#endif

/**
 * @brief Holds texture data with cached dimensions to avoid per-frame SDL_GetTextureSize() calls
 */
struct TextureData {
    std::shared_ptr<SDL_Texture> texture;
    float width{0.0f};
    float height{0.0f};
};

#ifdef USE_SDL3_GPU
/**
 * @brief Holds GPU texture data with cached dimensions
 */
struct GPUTextureData {
    std::unique_ptr<HammerEngine::GPUTexture> texture;
    float width{0.0f};
    float height{0.0f};
};

/**
 * @brief Pending texture upload data (surface held until upload completes)
 */
struct PendingTextureUpload {
    std::string textureID;
    std::unique_ptr<SDL_Surface, void(*)(SDL_Surface*)> surface;
    uint32_t width{0};
    uint32_t height{0};
};
#endif

class TextureManager {
 public:
 ~TextureManager() {
   if (!m_isShutdown) {
     clean();
   }
 }

 static TextureManager& Instance() {
   static TextureManager instance;
   return instance;
 }

  /**
   * @brief Loads a texture from a file or all PNG textures from a directory
   * @param fileName Path to texture file or directory containing PNG files
   * @param textureID Unique identifier for the texture(s). Used as prefix when loading directory
   * @param p_renderer SDL renderer for texture creation
   * @return true if at least one texture was loaded successfully, false otherwise
   */
  bool load(const std::string& fileName,
            const std::string& textureID,
            SDL_Renderer* p_renderer);

  /**
   * @brief Draws a texture to the renderer at specified position and size
   * @param textureID Unique identifier of the texture to draw
   * @param x X coordinate for drawing position
   * @param y Y coordinate for drawing position
   * @param width Width to draw the texture
   * @param height Height to draw the texture
   * @param p_renderer SDL renderer to draw to
   * @param flip Flip mode for the texture (default: SDL_FLIP_NONE)
   */
  void draw(const std::string& textureID,
            int x,
            int y,
            int width,
            int height,
            SDL_Renderer* p_renderer,
            SDL_FlipMode flip = SDL_FLIP_NONE);

  /**
   * @brief Draws a tile texture with perfect pixel alignment for tiled rendering
   * @param textureID Unique identifier of the texture to draw
   * @param x X coordinate for drawing position (float precision)
   * @param y Y coordinate for drawing position (float precision)
   * @param width Width to draw the texture
   * @param height Height to draw the texture
   * @param p_renderer SDL renderer to draw to
   * @param flip Flip mode for the texture (default: SDL_FLIP_NONE)
   */
  void drawTileF(const std::string& textureID,
                 float x,
                 float y,
                 int width,
                 int height,
                 SDL_Renderer* p_renderer,
                 SDL_FlipMode flip = SDL_FLIP_NONE);

  /**
   * @brief Draws a tile using a direct texture pointer (no hash lookup)
   * @param texture Direct SDL_Texture pointer (caller must ensure validity)
   * @param x X coordinate for drawing position (float precision)
   * @param y Y coordinate for drawing position (float precision)
   * @param width Width to draw the texture
   * @param height Height to draw the texture
   * @param p_renderer SDL renderer to draw to
   * @note Uses SDL_RenderTexture for optimal performance (no rotation overhead)
   */
  static void drawTileDirect(SDL_Texture* texture,
                             float x,
                             float y,
                             int width,
                             int height,
                             SDL_Renderer* p_renderer);

  /**
   * @brief Draws a sprite frame with float precision for smooth camera movement
   * @param textureID Unique identifier of the texture to draw
   * @param x X coordinate for drawing position (float precision)
   * @param y Y coordinate for drawing position (float precision)
   * @param width Width of the frame
   * @param height Height of the frame
   * @param currentRow Current animation row
   * @param currentFrame Current animation frame
   * @param p_renderer SDL renderer to draw to
   * @param flip Flip mode for the texture (default: SDL_FLIP_NONE)
   */
  void drawFrame(const std::string& textureID,
                 float x,
                 float y,
                 int width,
                 int height,
                 int currentRow,
                 int currentFrame,
                 SDL_Renderer* p_renderer,
                 SDL_FlipMode flip = SDL_FLIP_NONE);

  /**
   * @brief Draws a texture with parallax scrolling effect
   * @param textureID Unique identifier of the texture to draw
   * @param x X coordinate for drawing position
   * @param y Y coordinate for drawing position
   * @param scroll Scroll offset for parallax effect
   * @param p_renderer SDL renderer to draw to
   */
  void drawParallax(const std::string& textureID,
                    int x,
                    int y,
                    int scroll,
                    SDL_Renderer* p_renderer);
  /**
   * @brief Removes a texture from the texture map and frees its memory
   * @param textureID Unique identifier of the texture to remove
   */
  void clearFromTexMap(const std::string& textureID);
  /**
   * @brief Checks if a texture exists in the texture map
   * @param textureID Unique identifier of the texture to check
   * @return true if texture exists in map, false otherwise
   */
  bool isTextureInMap(const std::string& textureID) const;

  /**
   * @brief Retrieves a texture by its unique identifier
   * @param textureID Unique identifier of the texture to retrieve
   * @return Shared pointer to the texture, or nullptr if not found
   */
  std::shared_ptr<SDL_Texture> getTexture(const std::string& textureID) const;

  /**
   * @brief Get raw texture pointer for caching in entities (like WorldManager pattern)
   * @param textureID Unique identifier of the texture
   * @return Raw SDL_Texture pointer, or nullptr if not found. Caller must NOT free.
   * @note Use this for entity render caching to avoid per-frame hash lookups
   */
  SDL_Texture* getTexturePtr(const std::string& textureID) const;

  /**
   * @brief Creates or retrieves a cached dynamic texture (e.g., for chunk rendering)
   * @param textureID Unique identifier for the dynamic texture
   * @param width Width of the texture to create
   * @param height Height of the texture to create
   * @param p_renderer SDL renderer for texture creation
   * @param forceRecreate Force recreation even if texture exists
   * @return Shared pointer to the texture, creating it if it doesn't exist
   */
  std::shared_ptr<SDL_Texture> getOrCreateDynamicTexture(const std::string& textureID,
                                                         int width, int height,
                                                         SDL_Renderer* p_renderer,
                                                         bool forceRecreate = false);

  /**
   * @brief Cleans up all texture resources and marks manager as shut down
   */
  void clean();

  /**
   * @brief Checks if TextureManager has been shut down
   * @return true if manager is shut down, false otherwise
   */
  bool isShutdown() const { return m_isShutdown; }

#ifdef USE_SDL3_GPU
  /**
   * @brief Loads a texture for GPU rendering
   * @param fileName Path to texture file or directory
   * @param textureID Unique identifier for the texture
   * @return true if texture was loaded and queued for upload
   */
  bool loadGPU(const std::string& fileName, const std::string& textureID);

  /**
   * @brief Process pending GPU texture uploads during copy pass
   * @param copyPass Active GPU copy pass
   */
  void processPendingUploads(SDL_GPUCopyPass* copyPass);

  /**
   * @brief Get GPU texture by ID
   * @param textureID Unique identifier of the texture
   * @return Pointer to GPUTexture, or nullptr if not found
   */
  HammerEngine::GPUTexture* getGPUTexture(const std::string& textureID) const;

  /**
   * @brief Get GPU texture data (texture + dimensions)
   * @param textureID Unique identifier of the texture
   * @return Pointer to GPUTextureData, or nullptr if not found
   */
  const GPUTextureData* getGPUTextureData(const std::string& textureID) const;

  /**
   * @brief Check if GPU textures are available
   */
  bool hasGPUTextures() const { return !m_gpuTextureMap.empty(); }

  /**
   * @brief Check if there are pending uploads
   */
  bool hasPendingUploads() const { return !m_pendingUploads.empty(); }
#endif

 private:
  std::string m_textureID{""};
  std::unordered_map<std::string, TextureData> m_textureMap{};  // Stores texture + cached dimensions
  std::atomic<bool> m_texturesLoaded{false};
  std::mutex m_textureLoadMutex{};
  bool m_isShutdown{false};

#ifdef USE_SDL3_GPU
  /**
   * @brief Helper to load a single texture file for GPU rendering
   * @param fileName Path to texture file
   * @param textureID Unique identifier for the texture
   * @return true if texture was loaded and queued for upload
   */
  bool loadSingleGPUTexture(const std::string& fileName, const std::string& textureID);

  std::unordered_map<std::string, GPUTextureData> m_gpuTextureMap{};
  std::vector<PendingTextureUpload> m_pendingUploads{};
  mutable std::mutex m_gpuTextureMutex{};
#endif

  // Delete copy constructor and assignment operator
  TextureManager(const TextureManager&) = delete; //prevent copy construction
  TextureManager& operator=(const TextureManager&) = delete; //prevent assignment

  TextureManager() = default;

};

#endif  // TEXTURE_MANAGER_HPP
