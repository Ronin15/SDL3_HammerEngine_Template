/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/FontManager.hpp"
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <vector>

bool FontManager::init() {
  if (!TTF_Init()) {
    std::cerr << "Forge Game Engine - Font system initialization failed: " << SDL_GetError() << std::endl;
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

          // Load the individual file as a font with immediate RAII
          auto font = std::shared_ptr<TTF_Font>(TTF_OpenFont(fullPath.c_str(), fontSize), TTF_CloseFont);

          std::cout << "Forge Game Engine - Loading font: " << fullPath << "!\n";

          if (!font) {
            std::cerr << "Forge Game Engine - Could not load font: " << SDL_GetError() << std::endl;
            continue;
          }

          m_fontMap[combinedID] = std::move(font);
          loadedAny = true;
          fontsLoaded++;
        }
      }
    } catch (const std::filesystem::filesystem_error& e) {
      std::cerr << "Forge Game Engine - Filesystem error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
      std::cerr << "Forge Game Engine - Error while loading fonts: " << e.what() << std::endl;
    }

    std::cout << "Forge Game Engine - Loaded " << fontsLoaded << " fonts from directory: " << fontFile << "\n";
    return loadedAny; // Return true if at least one font was loaded successfully
  }

  // Standard single file loading with immediate RAII
  auto font = std::shared_ptr<TTF_Font>(TTF_OpenFont(fontFile.c_str(), fontSize), TTF_CloseFont);

  if (!font) {
    std::cerr << "Forge Game Engine - Failed to load font '" << fontFile <<
                 "': " << SDL_GetError() << std::endl;
    return false;
  }

  m_fontMap[fontID] = std::move(font);
  std::cout << "Forge Game Engine - Loaded font '" << fontID << "' from '" << fontFile << "'\n";
  return true;
}

std::shared_ptr<SDL_Texture> FontManager::renderText(
                                     const std::string& text, const std::string& fontID,
                                     SDL_Color color, SDL_Renderer* renderer) {
  // Skip if we're shutting down
  if (m_isShutdown) {
    std::cerr << "Forge Game Engine - Warning: Attempted to use FontManager after shutdown" << std::endl;
    return nullptr;
  }

  auto fontIt = m_fontMap.find(fontID);
  if (fontIt == m_fontMap.end()) {
    std::cerr << "Forge Game Engine - Font '" << fontID << "' not found." << std::endl;
    return nullptr;
  }

  // Check if text contains newlines - if so, handle multi-line rendering
  if (text.find('\n') != std::string::npos) {
    return renderMultiLineText(text, fontIt->second.get(), color, renderer);
  }

  // Render single line text to a surface using Blended mode (high quality with alpha) with immediate RAII
  auto surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
      TTF_RenderText_Blended(fontIt->second.get(), text.c_str(), 0, color), SDL_DestroySurface);
  if (!surface) {
    std::cerr << "Forge Game Engine - Failed to render text: " << SDL_GetError() << std::endl;
    return nullptr;
  }

  // Create texture from surface with immediate RAII
  auto texture = std::shared_ptr<SDL_Texture>(
      SDL_CreateTextureFromSurface(renderer, surface.get()), SDL_DestroyTexture);

  if (!texture) {
    std::cerr << "Forge Game Engine - Failed to create texture from rendered text: "
              << SDL_GetError() << std::endl;
    return nullptr;
  }

  return texture;
}

std::shared_ptr<SDL_Texture> FontManager::renderMultiLineText(
                                         const std::string& text, TTF_Font* font,
                                         SDL_Color color, SDL_Renderer* renderer) {
  // Split text by newlines
  std::vector<std::string> lines;
  std::string currentLine;
  for (char c : text) {
    if (c == '\n') {
      lines.push_back(currentLine);
      currentLine.clear();
    } else {
      currentLine += c;
    }
  }
  if (!currentLine.empty()) {
    lines.push_back(currentLine);
  }

  if (lines.empty()) {
    return nullptr;
  }

  // Get font height for line spacing
  int lineHeight = TTF_GetFontHeight(font);
  
  // Calculate total dimensions needed
  int maxWidth = 0;
  int totalHeight = lineHeight * lines.size();
  
  // Find the widest line
  for (const auto& line : lines) {
    int lineWidth = 0;
    if (!line.empty()) {
      TTF_GetStringSize(font, line.c_str(), 0, &lineWidth, nullptr);
    }
    maxWidth = std::max(maxWidth, lineWidth);
  }

  // Create a surface large enough for all lines
  auto combinedSurface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
      SDL_CreateSurface(maxWidth, totalHeight, SDL_PIXELFORMAT_RGBA8888), SDL_DestroySurface);
  
  if (!combinedSurface) {
    std::cerr << "Forge Game Engine - Failed to create combined surface: " << SDL_GetError() << std::endl;
    return nullptr;
  }

  // Fill with transparent background
  SDL_FillSurfaceRect(combinedSurface.get(), nullptr, SDL_MapRGBA(SDL_GetPixelFormatDetails(combinedSurface->format), nullptr, 0, 0, 0, 0));

  // Enable alpha blending for proper text compositing
  SDL_SetSurfaceBlendMode(combinedSurface.get(), SDL_BLENDMODE_BLEND);

  // Render each line and blit to combined surface
  int yOffset = 0;
  for (const auto& line : lines) {
    if (!line.empty()) {
      auto lineSurface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
          TTF_RenderText_Blended(font, line.c_str(), 0, color), SDL_DestroySurface);
      
      if (lineSurface) {
        // Ensure proper alpha blending when blitting
        SDL_SetSurfaceBlendMode(lineSurface.get(), SDL_BLENDMODE_NONE);
        SDL_Rect dstRect = {0, yOffset, lineSurface->w, lineSurface->h};
        SDL_BlitSurface(lineSurface.get(), nullptr, combinedSurface.get(), &dstRect);
      }
    }
    yOffset += lineHeight;
  }

  // Create texture from combined surface
  auto texture = std::shared_ptr<SDL_Texture>(
      SDL_CreateTextureFromSurface(renderer, combinedSurface.get()), SDL_DestroyTexture);

  if (!texture) {
    std::cerr << "Forge Game Engine - Failed to create texture from multi-line text: "
              << SDL_GetError() << std::endl;
    return nullptr;
  }

  // Set texture blend mode to preserve alpha
  SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);

  return texture;
}

void FontManager::drawText(const std::string& text, const std::string& fontID,
                          int x, int y, SDL_Color color, SDL_Renderer* renderer) {
  // Skip if we're shutting down
  if (m_isShutdown) {
    std::cerr << "Forge Game Engine - Warning: Attempted to use FontManager after shutdown" << std::endl;
    return;
  }

  auto texture = renderText(text, fontID, color, renderer);
  if (!texture) return;

  // Get the texture size
  float w, h;
  SDL_GetTextureSize(texture.get(), &w, &h);
  int width = static_cast<int>(w);
  int height = static_cast<int>(h);

  // Create a destination rectangle
  // Position x,y is considered to be the center of the text
  SDL_FRect dstRect = {static_cast<float>(x - width/2.0f), static_cast<float>(y - height/2.0f),
                      static_cast<float>(width), static_cast<float>(height)};

  // Render the texture
  SDL_RenderTexture(renderer, texture.get(), nullptr, &dstRect);

  // The texture will be automatically cleaned up when the unique_ptr goes out of scope
}

void FontManager::drawTextAligned(const std::string& text, const std::string& fontID,
                                 int x, int y, SDL_Color color, SDL_Renderer* renderer,
                                 int alignment) {
  // Skip if we're shutting down
  if (m_isShutdown) {
    std::cerr << "Forge Game Engine - Warning: Attempted to use FontManager after shutdown" << std::endl;
    return;
  }

  auto texture = renderText(text, fontID, color, renderer);
  if (!texture) return;

  // Get the texture size
  float w, h;
  SDL_GetTextureSize(texture.get(), &w, &h);
  int width = static_cast<int>(w);
  int height = static_cast<int>(h);

  // Calculate position based on alignment
  float destX, destY;
  
  switch (alignment) {
    case 1: // Left alignment
      destX = static_cast<float>(x);
      destY = static_cast<float>(y - height/2.0f);
      break;
    case 2: // Right alignment
      destX = static_cast<float>(x - width);
      destY = static_cast<float>(y - height/2.0f);
      break;
    case 3: // Top-left alignment
      destX = static_cast<float>(x);
      destY = static_cast<float>(y);
      break;
    case 4: // Top-center alignment
      destX = static_cast<float>(x - width/2.0f);
      destY = static_cast<float>(y);
      break;
    case 5: // Top-right alignment
      destX = static_cast<float>(x - width);
      destY = static_cast<float>(y);
      break;
    default: // Center alignment (0)
      destX = static_cast<float>(x - width/2.0f);
      destY = static_cast<float>(y - height/2.0f);
      break;
  }

  // Create a destination rectangle
  SDL_FRect dstRect = {destX, destY, static_cast<float>(width), static_cast<float>(height)};

  // Render the texture
  SDL_RenderTexture(renderer, texture.get(), nullptr, &dstRect);

  // The texture will be automatically cleaned up when the unique_ptr goes out of scope
}

bool FontManager::isFontLoaded(const std::string& fontID) const {
  return m_fontMap.find(fontID) != m_fontMap.end();
}

void FontManager::clearFont(const std::string& fontID) {
  // No need to manually call TTF_CloseFont as the unique_ptr will handle it
  if (m_fontMap.erase(fontID) > 0) {
    std::cout << "Forge Game Engine - Cleared font: " << fontID << "\n";
  }
}

void FontManager::clean() {

// Track the number of fonts cleaned up
int fontsFreed = m_fontMap.size();
// Mark the manager as shutting down before freeing resources
m_isShutdown = true;

  // No need to manually close fonts as the unique_ptr will handle it
  m_fontMap.clear();

  std::cout << "Forge Game Engine - "<< fontsFreed << " fonts Freed!\n";
  std::cout << "Forge Game Engine - FontManager resources cleaned!\n";
  // Ensure TTF system is properly shut down
  TTF_Quit();
}
