// Copyright (c) 2025 Hammer Forged Games
// Licensed under the MIT License - see LICENSE file for details

#include "FontManager.hpp"
#include <iostream>
#include <algorithm>
#include <filesystem>

TTF_TextEngine* FontManager::m_rendererTextEngine = nullptr;

bool FontManager::init() {
  if (!TTF_Init()) {
    std::cerr << "Forge Game Engine - Font system initialization failed: " << SDL_GetError() << "\n";
      return false;
  } else {

    std::cout << "Forge Game Engine - Font system initialized!\n";
      return true;
  }
}

bool FontManager::loadFont(const std::string& fontFile, const std::string& fontID, int fontSize) {
  // Check if the fontFile is a directory
  if (std::filesystem::exists(fontFile) && std::filesystem::is_directory(fontFile)) {
    std::cout << "Forge Game Engine - Loading fonts from directory: " << fontFile << "\n";

    bool loadedAny = false;
    int fontsLoaded = 0;

    try {
      // Iterate through all files in the directory
      for (const auto& entry : std::filesystem::directory_iterator(fontFile)) {
        if (!entry.is_regular_file()) {
          continue; // Skip directories and special files
        }

        // Get file path and extension
        std::filesystem::path path = entry.path();
        std::string extension = path.extension().string();

        // Convert extension to lowercase for case-insensitive comparison
        std::transform(extension.begin(), extension.end(), extension.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        // Check if the file has a supported font extension
        if (extension == ".ttf" || extension == ".otf") {
          std::string fullPath = path.string();
          std::string filename = path.stem().string(); // Get filename without extension

          // Create font ID by combining the provided prefix and filename
          std::string combinedID = fontID.empty() ? filename : fontID + "_" + filename;

          // Load the individual file as a font
          TTF_Font* font = TTF_OpenFont(fullPath.c_str(), fontSize);

          std::cout << "Forge Game Engine - Loading font: " << fullPath << "!\n";

          if (font == nullptr) {
            std::cerr << "Forge Game Engine - Could not load font: " << SDL_GetError() << "\n";
            continue;
          }

          m_fontMap[combinedID] = font;
          loadedAny = true;
          fontsLoaded++;
        }
      }
    } catch (const std::filesystem::filesystem_error& e) {
      std::cerr << "Forge Game Engine - Filesystem error: " << e.what() << "\n";
    } catch (const std::exception& e) {
      std::cerr << "Forge Game Engine - Error while loading fonts: " << e.what() << "\n";
    }

    std::cout << "Forge Game Engine - Loaded " << fontsLoaded << " fonts from directory: " << fontFile << "\n";
    return loadedAny; // Return true if at least one font was loaded successfully
  }

  // Standard single file loading code
  TTF_Font* font = TTF_OpenFont(fontFile.c_str(), fontSize);

  if (font == nullptr) {
    std::cerr << "Forge Game Engine - Failed to load font '" << fontFile <<
                 "': " << SDL_GetError() << "\n";
    return false;
  }

  m_fontMap[fontID] = font;
  std::cout << "Forge Game Engine - Loaded font '" << fontID << "' from '" << fontFile << "'\n";
  return true;
}

SDL_Texture* FontManager::renderText(const std::string& text, const std::string& fontID,
                                     SDL_Color color, SDL_Renderer* renderer) {
  // Skip if we're shutting down
  if (m_isShutdown) {
    std::cerr << "Forge Game Engine - Warning: Attempted to use FontManager after shutdown" << std::endl;
    return nullptr;
  }

  auto fontIt = m_fontMap.find(fontID);
  if (fontIt == m_fontMap.end()) {
    std::cerr << "Forge Game Engine - Font '" << fontID << "' not found.\n";
    return nullptr;
  }

  // Render the text to a surface using Blended mode (high quality with alpha)
  SDL_Surface* surface = TTF_RenderText_Blended(fontIt->second, text.c_str(), 0, color);
  if (!surface) {
    std::cerr << "Forge Game Engine - Failed to render text: " << SDL_GetError() << "\n";
    return nullptr;
  }

  // Create texture from surface
  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_DestroySurface(surface);

  if (!texture) {
    std::cerr << "Forge Game Engine - Failed to create texture from rendered text: "
              << SDL_GetError() << "\n";
  }

  return texture;
}

void FontManager::drawText(const std::string& text, const std::string& fontID,
                          int x, int y, SDL_Color color, SDL_Renderer* renderer) {
  // Skip if we're shutting down
  if (m_isShutdown) {
    std::cerr << "Forge Game Engine - Warning: Attempted to use FontManager after shutdown" << std::endl;
    return;
  }

  SDL_Texture* texture = renderText(text, fontID, color, renderer);
  if (!texture) return;

  // Get the texture size
  float w, h;
  SDL_GetTextureSize(texture, &w, &h);
  int width = static_cast<int>(w);
  int height = static_cast<int>(h);

  // Create a destination rectangle
  // Position x,y is considered to be the center of the text
  SDL_FRect dstRect = {static_cast<float>(x - width/2.0f), static_cast<float>(y - height/2.0f),
                      static_cast<float>(width), static_cast<float>(height)};

  // Render the texture
  SDL_RenderTexture(renderer, texture, nullptr, &dstRect);

  // Clean up
  SDL_DestroyTexture(texture);
}

bool FontManager::isFontLoaded(const std::string& fontID) const {
  return m_fontMap.find(fontID) != m_fontMap.end();
}

void FontManager::clearFont(const std::string& fontID) {
  if (m_fontMap.find(fontID) != m_fontMap.end()) {
    TTF_CloseFont(m_fontMap[fontID]);
    m_fontMap.erase(fontID);
    std::cout << "Forge Game Engine - Cleared font: " << fontID << std::endl;
  }
}

void FontManager::clean() {
  std::cout << "Forge Game Engine - FontManager resources cleaned!\n";

  // Mark the manager as shutting down before freeing resources
  m_isShutdown = true;

  // Close all fonts
  for (auto& font : m_fontMap) {
    if (font.second) {
      TTF_CloseFont(font.second);
      font.second = nullptr;
    }
  }

  m_fontMap.clear();

  // Ensure TTF system is properly shut down
  TTF_Quit();
}
