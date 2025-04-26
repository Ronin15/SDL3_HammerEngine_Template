// Copyright (c) 2025 Hammer Forged Games
// Licensed under the MIT License - see LICENSE file for details

#include "TextureManager.hpp"
#include <iostream>
#include <filesystem>
#include <algorithm>

bool TextureManager::load(std::string fileName,
                          std::string textureID,
                          SDL_Renderer* p_renderer) {
  // Check if the fileName is a directory
  if (std::filesystem::exists(fileName) && std::filesystem::is_directory(fileName)) {
    std::cout << "Forge Game Engine - Loading textures from directory: " << fileName << "\n";

    bool loadedAny = false;
    int texturesLoaded = 0;

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
            m_textureMap[combinedID] = p_texture;
            loadedAny = true;
            texturesLoaded++;
          } else {
            std::cerr << "Forge Game Engine - Could not create texture: " << SDL_GetError() << "\n";
          }
        }
      }
    } catch (const std::filesystem::filesystem_error& e) {
      std::cerr << "Forge Game Engine - Filesystem error: " << e.what() << "\n";
    } catch (const std::exception& e) {
      std::cerr << "Forge Game Engine - Error while loading textures: " << e.what() << "\n";
    }

    std::cout << "Forge Game Engine - Loaded " << texturesLoaded << " textures from directory: " << fileName << "\n";
    return loadedAny; // Return true if at least one texture was loaded successfully
  }

  // Standard single file loading code
  SDL_Surface* p_tempSurface = IMG_Load(fileName.c_str());

  std::cout << "Forge Game Engine - Loading texture: " << fileName << "!\n";

  if (p_tempSurface == 0) {
    std::cerr << "Forge Game Engine - Could not load image: " << SDL_GetError() << "\n";

    return false;
  }

  SDL_Texture* p_texture = SDL_CreateTextureFromSurface(p_renderer, p_tempSurface);

  SDL_DestroySurface(p_tempSurface);

  if (p_texture != 0) {
    m_textureMap[textureID] = p_texture;
    return true;
  }

  std::cerr << "Forge Game Engine - Could not create Texture: " << SDL_GetError() << "\n";

  return false;
}

void TextureManager::draw(std::string textureID,
                          int x,
                          int y,
                          int width,
                          int height,
                          SDL_Renderer* p_renderer,
                          SDL_FlipMode flip) {
  SDL_FRect srcRect;
  SDL_FRect destRect;
  SDL_FPoint center = {0, 0};  // Initialize center point
  double angle = 0.0;

  srcRect.x = 0;
  srcRect.y = 0;
  srcRect.w = destRect.w = width;
  srcRect.h = destRect.h = height;
  destRect.x = x;
  destRect.y = y;

  SDL_RenderTextureRotated(p_renderer, m_textureMap[textureID], &srcRect, &destRect, angle, &center, flip);
}

void TextureManager::drawFrame(std::string textureID,
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
  SDL_FPoint center = {0, 0};  // Initialize center point
  double angle = 0.0;

  srcRect.x = width * currentFrame;
  srcRect.y = height * (currentRow - 1);
  srcRect.w = destRect.w = width;
  srcRect.h = destRect.h = height;
  destRect.x = x;
  destRect.y = y;

  SDL_RenderTextureRotated(p_renderer, m_textureMap[textureID], &srcRect, &destRect, angle, &center, flip);
}
/*
void TextureManager::drawParallax(std::string textureID, int x, int y, int
width, int height, int scroll, SDL_Renderer* pRenderer) {

  SDL_Rect srcRect1;
  SDL_Rect destRect1;

  SDL_Rect srcRect2;
  SDL_Rect destRect2;
  scroll = scroll % width;
  srcRect1.x = 0;
  srcRect1.y = 0;
  srcRect1.w = destRect1.w = width;
  srcRect1.h = destRect1.h = height;
  destRect1.x = x;
  destRect1.y = y;

  srcRect2.x = (0 - width) + 1;
  srcRect2.y = 0;
  srcRect2.w = destRect2.w = width;
  srcRect2.h = destRect2.h = height;
  destRect2.x = x;
  destRect2.y = y;

  SDL_RenderCopy(pRenderer, textureMap_[textureID], &srcRect1, &destRect1);
  SDL_RenderCopy(pRenderer, textureMap_[textureID], &srcRect2, &destRect2);
}
*/
void TextureManager::clearFromTexMap(std::string textureID) {
    std::cout << "Forge Game Engine - Cleared : " << textureID << " texture" << std::endl;
  m_textureMap.erase(textureID);
}

bool TextureManager::isTextureInMap(std::string textureID) const {
  return m_textureMap.find(textureID) != m_textureMap.end();
}

SDL_Texture* TextureManager::getTexture(std::string textureID) const {
  // Check if the texture exists in the map
  auto it = m_textureMap.find(textureID);
  if (it != m_textureMap.end()) {
    return it->second;
  }

  // Return nullptr if the texture is not found
  return nullptr;
}

void TextureManager::clean() {
  std::cout << "Forge Game Engine - Cleaning up TextureManager resources" << std::endl;

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

  std::cout << "Forge Game Engine - Freed " << texturesFreed << " textures" << std::endl;
}
