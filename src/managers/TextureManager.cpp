/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/TextureManager.hpp"
#include <iostream>
#include <filesystem>
#include <algorithm>

bool TextureManager::initialized = false; // Initialize the static variable for TextureManager initialization

bool TextureManager::load(const std::string& fileName,
                          const std::string& textureID,
                          SDL_Renderer* p_renderer) {
  // Check if the fileName is a directory
  if (std::filesystem::exists(fileName) && std::filesystem::is_directory(fileName)) {
    std::cout << "Forge Game Engine - Loading textures from directory: " << fileName << "\n";

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

          // Load the individual file as a texture
          // Call the standard loading code directly rather than calling load() recursively
          SDL_Surface* p_tempSurface = IMG_Load(fullPath.c_str());

          std::cout << "Forge Game Engine - Loading texture: " << fullPath << "!\n";

          if (p_tempSurface == 0) {
            std::cout << "Forge Game Engine - Could not load image: " << SDL_GetError() << "\n";
            continue;
          }

          SDL_Texture* p_texture = SDL_CreateTextureFromSurface(p_renderer, p_tempSurface);

          SDL_DestroySurface(p_tempSurface);

          if (p_texture != 0) {
            //SDL_SetTextureBlendMode(p_texture, SDL_BLENDMODE_ADD); //for lighting // this puts light on by default
            m_textureMap[combinedID] = p_texture;
            loadedAny = true;
            texturesLoaded++;
          } else {
            std::cerr << "Forge Game Engine - Could not create texture: " << SDL_GetError() << std::endl;
          }
        }
      }
    } catch (const std::filesystem::filesystem_error& e) {
      std::cerr << "Forge Game Engine - Filesystem error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
      std::cerr << "Forge Game Engine - Error while loading textures: " << e.what() << std::endl;
    }

    std::cout << "Forge Game Engine - Loaded " << texturesLoaded << " textures from directory: " << fileName << "\n";
    return loadedAny; // Return true if at least one texture was loaded successfully
  }

  // Standard single file loading code
  SDL_Surface* p_tempSurface = IMG_Load(fileName.c_str());

  std::cout << "Forge Game Engine - Loading texture: " << fileName << "!\n";

  if (p_tempSurface == 0) {
    std::cerr << "Forge Game Engine - Could not load image: " << SDL_GetError() << std::endl;

    return false;
  }

  SDL_Texture* p_texture = SDL_CreateTextureFromSurface(p_renderer, p_tempSurface);

  SDL_DestroySurface(p_tempSurface);

  if (p_texture != 0) {
    m_textureMap[textureID] = p_texture;
    return true;
  }

  std::cerr << "Forge Game Engine - Could not create Texture: " << SDL_GetError() << std::endl;

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

  SDL_RenderTextureRotated(p_renderer, m_textureMap[textureID], &srcRect, &destRect, angle, &center, flip);
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

  SDL_RenderTextureRotated(p_renderer, m_textureMap[textureID], &srcRect, &destRect, angle, &center, flip);
}

void TextureManager::drawParallax(const std::string& textureID,
                    int x,
                    int y,
                    int scroll,
                    SDL_Renderer* p_renderer) {
  // Verify the texture exists
  auto it = m_textureMap.find(textureID);
  if (it == m_textureMap.end()) {
    std::cerr << "Forge Game Engine - Texture not found: " << textureID << std::endl;
    return;
  }

  // Get the texture dimensions
  float width, height;
  if (SDL_GetTextureSize(it->second, &width, &height) != 0) {
    std::cerr << "Forge Game Engine - Failed to get texture size: " << SDL_GetError() << std::endl;
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
  SDL_RenderTexture(p_renderer, it->second, &srcRect1, &destRect1);
  SDL_RenderTexture(p_renderer, it->second, &srcRect2, &destRect2);
}

void TextureManager::clearFromTexMap(const std::string& textureID) {
    std::cout << "Forge Game Engine - Cleared : " << textureID << " texture\n";
  m_textureMap.erase(textureID);
}

bool TextureManager::isTextureInMap(const std::string& textureID) const {
  return m_textureMap.find(textureID) != m_textureMap.end();
}

SDL_Texture* TextureManager::getTexture(const std::string& textureID) const {
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
  int texturesFreed = 0;

  // Destroy all textures in the map
  for (auto& texturePair : m_textureMap) {
    if (texturePair.second != nullptr) {
      SDL_DestroyTexture(texturePair.second);
      texturePair.second = nullptr;
      texturesFreed++;
    }
  }

  // Clear the map
  m_textureMap.clear();

  std::cout << "Forge Game Engine - "<< texturesFreed << " textures Freed!\n";
  std::cout << "Forge Game Engine - TextureManager resources cleaned!\n";
}
