/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/TextureManager.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <filesystem>



bool TextureManager::load(const std::string& fileName,
                          const std::string& textureID,
                          SDL_Renderer* p_renderer) {
  if (m_texturesLoaded.load(std::memory_order_acquire)) {
    return true;
  }

  std::lock_guard<std::mutex> lock(m_textureLoadMutex);
  if (m_texturesLoaded.load(std::memory_order_acquire)) {
    return true;
  }

  // Check if the fileName is a directory
  if (std::filesystem::exists(fileName) && std::filesystem::is_directory(fileName)) {
    TEXTURE_INFO("Loading textures from directory: " + fileName);

    bool loadedAny = false;
    int texturesLoaded{0};

    try {
      // Iterate through all files in the directory
      for (const auto& entry : std::filesystem::directory_iterator(fileName)) {
        if (!entry.is_regular_file()) {
          continue; // Skip directories and special files
        }

        // Get file path and extension
        std::filesystem::path filePath = entry.path();
        std::string extension = filePath.extension().string();

        // Convert extension to lowercase for case-insensitive comparison
        std::transform(extension.begin(), extension.end(), extension.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        // Check if the file is a PNG
        if (extension == ".png") {
          std::string fullPath = filePath.string();
          std::string filename = filePath.stem().string(); // Get filename without extension

          // Create texture ID by combining the provided prefix and filename
          std::string combinedID = textureID.empty() ? filename : textureID + "_" + filename;

          // Load the individual file as a texture with immediate RAII
          auto surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
              IMG_Load(fullPath.c_str()), SDL_DestroySurface);

          TEXTURE_INFO("Loading texture: " + fullPath);

          if (!surface) {
            TEXTURE_ERROR("Could not load image: " + std::string(SDL_GetError()));
            continue;
          }

          auto texture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>(
              SDL_CreateTextureFromSurface(p_renderer, surface.get()), SDL_DestroyTexture);

          if (texture) {
            // Fix for tile rendering artifacts on macOS (Metal backend) by forcing nearest-pixel sampling.
            SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST);
            //SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_ADD); //for lighting // this puts light on by default
            m_textureMap[combinedID] = std::shared_ptr<SDL_Texture>(texture.release(), SDL_DestroyTexture);
            loadedAny = true;
            texturesLoaded++;
          } else {
            TEXTURE_ERROR("Could not create texture: " + std::string(SDL_GetError()));
          }
        }
      }
    } catch (const std::filesystem::filesystem_error& e) {
      TEXTURE_ERROR("Filesystem error: " + std::string(e.what()));
    } catch (const std::exception& e) {
      TEXTURE_ERROR("Error while loading textures: " + std::string(e.what()));
    }

    TEXTURE_INFO("Loaded " + std::to_string(texturesLoaded) + " textures from directory: " + fileName);

    // Suppress unused variable warning in release builds
    (void)texturesLoaded;
    if(loadedAny) m_texturesLoaded.store(true, std::memory_order_release);
    return loadedAny; // Return true if at least one texture was loaded successfully
  }

  // Standard single file loading with immediate RAII
  auto surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
      IMG_Load(fileName.c_str()), SDL_DestroySurface);

  TEXTURE_INFO("Loaded texture: " + textureID);

  if (!surface) {
    TEXTURE_ERROR("Could not load image: " + std::string(SDL_GetError()));
      return false;
  }

  auto texture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>(
      SDL_CreateTextureFromSurface(p_renderer, surface.get()), SDL_DestroyTexture);

  if (texture) {
    // Fix for tile rendering artifacts on macOS (Metal backend) by forcing nearest-pixel sampling.
    SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST);
    m_textureMap[textureID] = std::shared_ptr<SDL_Texture>(texture.release(), SDL_DestroyTexture);
    m_texturesLoaded.store(true, std::memory_order_release);
    return true;
  }

  TEXTURE_ERROR("Could not create texture: " + std::string(SDL_GetError()));

  return false;
}

void TextureManager::draw(const std::string& textureID,
                          int x,
                          int y,
                          int width,
                          int height,
                          SDL_Renderer* p_renderer,
                          SDL_FlipMode flip) {
  // Get actual texture dimensions for source rectangle
  float texWidth, texHeight;
  if (!SDL_GetTextureSize(m_textureMap[textureID].get(), &texWidth, &texHeight)) {
    TEXTURE_ERROR("Failed to get texture size for '" + textureID + "': " + std::string(SDL_GetError()));
    return;
  }

  SDL_FRect srcRect;
  SDL_FRect destRect;
  SDL_FPoint center = {static_cast<float>(width) / 2.0f, static_cast<float>(height) / 2.0f};  // Center point in the middle of the image
  double angle = 0.0;

  // Inset source rectangle by a small amount to prevent texture bleeding
  // Use actual texture dimensions, not destination dimensions
  srcRect.x = 0.1f;
  srcRect.y = 0.1f;
  srcRect.w = texWidth - 0.2f;
  srcRect.h = texHeight - 0.2f;

  // Destination rectangle uses requested width/height for scaling
  destRect.w = static_cast<float>(width);
  destRect.h = static_cast<float>(height);
  destRect.x = static_cast<float>(x);
  destRect.y = static_cast<float>(y);

  SDL_RenderTextureRotated(p_renderer, m_textureMap[textureID].get(), &srcRect, &destRect, angle, &center, flip);
}

void TextureManager::drawF(const std::string& textureID,
                           float x,
                           float y,
                           int width,
                           int height,
                           SDL_Renderer* p_renderer,
                           SDL_FlipMode flip) {
  // Get actual texture dimensions for source rectangle
  float texWidth, texHeight;
  if (!SDL_GetTextureSize(m_textureMap[textureID].get(), &texWidth, &texHeight)) {
    TEXTURE_ERROR("Failed to get texture size for '" + textureID + "': " + std::string(SDL_GetError()));
    return;
  }

  SDL_FRect srcRect;
  SDL_FRect destRect;
  SDL_FPoint center = {static_cast<float>(width) / 2.0f, static_cast<float>(height) / 2.0f};  // Center point in the middle of the image
  double angle = 0.0;

  // Inset source rectangle by a small amount to prevent texture bleeding
  // Use actual texture dimensions, not destination dimensions
  srcRect.x = 0.1f;
  srcRect.y = 0.1f;
  srcRect.w = texWidth - 0.2f;
  srcRect.h = texHeight - 0.2f;

  // Destination rectangle uses requested width/height for scaling
  destRect.w = static_cast<float>(width);
  destRect.h = static_cast<float>(height);
  destRect.x = x;  // Use float precision directly
  destRect.y = y;  // Use float precision directly

  SDL_RenderTextureRotated(p_renderer, m_textureMap[textureID].get(), &srcRect, &destRect, angle, &center, flip);
}

void TextureManager::drawTileF(const std::string& textureID,
                               float x,
                               float y,
                               int width,
                               int height,
                               SDL_Renderer* p_renderer,
                               SDL_FlipMode flip) {
  SDL_FRect srcRect;
  SDL_FRect destRect;
  SDL_FPoint center = {static_cast<float>(width) / 2.0f, static_cast<float>(height) / 2.0f};
  double angle = 0.0;

  // Perfect pixel source rectangle - no inset for seamless tiling
  srcRect.x = 0.0f;
  srcRect.y = 0.0f;
  srcRect.w = static_cast<float>(width);
  srcRect.h = static_cast<float>(height);

  destRect.w = static_cast<float>(width);
  destRect.h = static_cast<float>(height);
  destRect.x = x;  // Use sub-pixel precision - SDL3/GPU handles smooth rendering
  destRect.y = y;  // Use sub-pixel precision - SDL3/GPU handles smooth rendering

  SDL_RenderTextureRotated(p_renderer, m_textureMap[textureID].get(), &srcRect, &destRect, angle, &center, flip);
}

void TextureManager::drawFrame(const std::string& textureID,
                               int x,
                               int y,
                               int width,
                               int height,
                               int currentRow,
                               int currentFrame,
                               SDL_Renderer* p_renderer,
                               SDL_FlipMode flip) {
  SDL_FRect srcRect;
  SDL_FRect destRect;
  SDL_FPoint center = {static_cast<float>(width) / 2.0f, static_cast<float>(height) / 2.0f};  // Center point in the middle of the image
  double angle = 0.0;

  // Inset source rectangle to prevent texture bleeding
  srcRect.x = static_cast<float>(width * currentFrame) + 0.1f;
  srcRect.y = static_cast<float>(height * (currentRow - 1)) + 0.1f;
  srcRect.w = static_cast<float>(width) - 0.2f;
  srcRect.h = static_cast<float>(height) - 0.2f;

  destRect.w = static_cast<float>(width);
  destRect.h = static_cast<float>(height);
  destRect.x = static_cast<float>(x);
  destRect.y = static_cast<float>(y);

  SDL_RenderTextureRotated(p_renderer, m_textureMap[textureID].get(), &srcRect, &destRect, angle, &center, flip);
}

void TextureManager::drawFrameF(const std::string& textureID,
                                float x,
                                float y,
                                int width,
                                int height,
                                int currentRow,
                                int currentFrame,
                                SDL_Renderer* p_renderer,
                                SDL_FlipMode flip) {
  SDL_FRect srcRect;
  SDL_FRect destRect;
  SDL_FPoint center = {static_cast<float>(width) / 2.0f, static_cast<float>(height) / 2.0f};  // Center point in the middle of the image
  double angle = 0.0;

  // Use exact source pixel bounds for sprite frames to avoid subpixel sampling jitter
  // Entities render at integer-aligned screen positions; exact src rects prevent hitching when camera moves
  srcRect.x = static_cast<float>(width * currentFrame);
  srcRect.y = static_cast<float>(height * (currentRow - 1));
  srcRect.w = static_cast<float>(width);
  srcRect.h = static_cast<float>(height);

  // Use float precision directly - no casting from integer
  destRect.w = static_cast<float>(width);
  destRect.h = static_cast<float>(height);
  destRect.x = x;  // Direct float assignment
  destRect.y = y;  // Direct float assignment

  SDL_RenderTextureRotated(p_renderer, m_textureMap[textureID].get(), &srcRect, &destRect, angle, &center, flip);
}

void TextureManager::drawParallax(const std::string& textureID,
                    int x,
                    int y,
                    int scroll,
                    SDL_Renderer* p_renderer) {
  // Verify the texture exists
  auto it = m_textureMap.find(textureID);
  if (it == m_textureMap.end()) {
    TEXTURE_WARN("Texture not found: " + textureID);
    return;
  }

  // Get the texture dimensions
  float width, height;
  if (!SDL_GetTextureSize(it->second.get(), &width, &height)) {
    TEXTURE_ERROR("Failed to get texture size: " + std::string(SDL_GetError()));
    return;
  }

  // Calculate scroll offset (make sure it wraps around)
  scroll = scroll % static_cast<int>(width);
  if (scroll < 0) {
    scroll += static_cast<int>(width); // Handle negative scroll values
  }

  SDL_FRect srcRect1, destRect1, srcRect2, destRect2;

  // First part of the background
  srcRect1.x = static_cast<float>(scroll);
  srcRect1.y = 0;
  srcRect1.w = width - static_cast<float>(scroll);
  srcRect1.h = height;

  destRect1.x = static_cast<float>(x);
  destRect1.y = static_cast<float>(y);
  destRect1.w = srcRect1.w;
  destRect1.h = height;

  // Second part of the background (wrapping around)
  srcRect2.x = 0;
  srcRect2.y = 0;
  srcRect2.w = static_cast<float>(scroll);
  srcRect2.h = height;

  destRect2.x = static_cast<float>(x) + srcRect1.w;
  destRect2.y = static_cast<float>(y);
  destRect2.w = srcRect2.w;
  destRect2.h = height;

  // Draw the two parts of the parallax background without rotation
  SDL_RenderTexture(p_renderer, it->second.get(), &srcRect1, &destRect1);
  SDL_RenderTexture(p_renderer, it->second.get(), &srcRect2, &destRect2);
}

void TextureManager::clearFromTexMap(const std::string& textureID) {
    TEXTURE_INFO("Cleared : " + textureID + " texture");
  m_textureMap.erase(textureID);
}

bool TextureManager::isTextureInMap(const std::string& textureID) const {
  return m_textureMap.find(textureID) != m_textureMap.end();
}

std::shared_ptr<SDL_Texture> TextureManager::getTexture(const std::string& textureID) const {
  // Check if the texture exists in the map
  auto it = m_textureMap.find(textureID);
  if (it != m_textureMap.end()) {
    return it->second;
  }

  // Return nullptr if the texture is not found
  return nullptr;
}

std::shared_ptr<SDL_Texture> TextureManager::getOrCreateDynamicTexture(const std::string& textureID,
                                                                       int width, int height,
                                                                       SDL_Renderer* p_renderer,
                                                                       bool forceRecreate) {
  if (m_isShutdown || !p_renderer) {
    return nullptr;
  }

  // Check if texture already exists in cache
  auto it = m_textureMap.find(textureID);
  if (it != m_textureMap.end() && !forceRecreate) {
    return it->second;
  }

  // Remove old texture if recreating
  if (forceRecreate && it != m_textureMap.end()) {
    m_textureMap.erase(it);
  }

  // Create new dynamic texture
  SDL_Texture* rawTexture = SDL_CreateTexture(p_renderer, SDL_PIXELFORMAT_RGBA8888, 
                                             SDL_TEXTUREACCESS_TARGET, width, height);
  if (!rawTexture) {
    TEXTURE_ERROR("Failed to create dynamic texture: " + textureID);
    return nullptr;
  }

  // Wrap in shared_ptr and add to cache
  std::shared_ptr<SDL_Texture> texture(rawTexture, SDL_DestroyTexture);
  m_textureMap[textureID] = texture;

  return texture;
}

void TextureManager::clean() {
  if (m_isShutdown) {
    return;
  }

  // Clear all textures before SDL shutdown
  m_textureMap.clear();
  m_isShutdown = true;
}
