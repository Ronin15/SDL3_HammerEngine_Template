/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/TextureManager.hpp"
#include "core/Logger.hpp"
#include <filesystem>
#include <algorithm>



bool TextureManager::load(const std::string& fileName,
                          const std::string& textureID,
                          SDL_Renderer* p_renderer) {
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
    m_textureMap[textureID] = std::shared_ptr<SDL_Texture>(texture.release(), SDL_DestroyTexture);
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
  SDL_FRect srcRect;
  SDL_FRect destRect;
  SDL_FPoint center = {width / 2.0f, height / 2.0f};  // Center point in the middle of the image
  double angle = 0.0;

  srcRect.x = 0;
  srcRect.y = 0;
  srcRect.w = width;
  srcRect.h = height;
  destRect.w = width;
  destRect.h = height;
  destRect.x = x;
  destRect.y = y;

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
  SDL_FPoint center = {width / 2.0f, height / 2.0f};  // Center point in the middle of the image
  double angle = 0.0;

  srcRect.x = width * currentFrame;
  srcRect.y = height * (currentRow - 1);
  srcRect.w = width;
  srcRect.h = height;
  destRect.w = width;
  destRect.h = height;
  destRect.x = x;
  destRect.y = y;

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
  if (SDL_GetTextureSize(it->second.get(), &width, &height) != 0) {
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

void TextureManager::clean() {

  // Track the number of textures cleaned up
  [[maybe_unused]] int texturesFreed = m_textureMap.size();

  // Clear the map - shared_ptr will automatically destroy the textures
  m_textureMap.clear();

  // Set shutdown flag
  m_isShutdown = true;

  TEXTURE_INFO(std::to_string(texturesFreed) + " textures freed");
  TEXTURE_INFO("TextureManager resources cleaned");
}
