/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/FontManager.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <filesystem>
#include <vector>
#include <cmath>
#include <sstream>

// Font sizing configuration constants
namespace {
  // Platform-specific base font sizes
  [[maybe_unused]] constexpr float APPLE_BASE_FONT_SIZE = 18.0f;
  [[maybe_unused]] constexpr float NON_APPLE_HEIGHT_RATIO = 90.0f;
  
  // Font size ratios for different text types
  #ifdef __APPLE__
  constexpr float UI_FONT_RATIO = 1.0f;       // 100% of base (slightly larger for macOS)
  #else
  constexpr float UI_FONT_RATIO = 0.875f;     // 87.5% of base
  #endif
  constexpr float TITLE_FONT_RATIO = 1.5f;    // 150% of base
  constexpr float TOOLTIP_FONT_RATIO = 0.6f;  // 60% of base
  
  // Font size bounds for edge case protection
  constexpr int MIN_FONT_SIZE = 8;
  constexpr int MAX_FONT_SIZE = 100;
  
  // Minimum readable sizes for specific font types (ensure good readability at 1080p)
  constexpr int MIN_BASE_FONT_SIZE = 18;
  constexpr int MIN_UI_FONT_SIZE = 16;
  constexpr int MIN_TITLE_FONT_SIZE = 24;
  constexpr int MIN_TOOLTIP_FONT_SIZE = 12;
}

bool FontManager::init() {
  if (!TTF_Init()) {
    FONT_CRITICAL("Font system initialization failed: " + std::string(SDL_GetError()));
      return false;
  } else {
    // Reset shutdown flag when reinitializing
    m_isShutdown = false;
    FONT_INFO("Font system initialized with quality hints!");
      return true;
  }
}

bool FontManager::loadFontsForDisplay(const std::string& fontPath, int windowWidth, int windowHeight) {
    if (m_fontsLoaded.load(std::memory_order_acquire)) {
        return true;
    }

    std::lock_guard<std::mutex> lock(m_fontLoadMutex);
    // Double-check after acquiring the lock
    if (m_fontsLoaded.load(std::memory_order_acquire)) {
        return true;
    }

    // Scan directory for font files if not already done
    if (m_fontFilePaths.empty()) {
        try {
            for (const auto& entry : std::filesystem::directory_iterator(fontPath)) {
                if (entry.is_regular_file()) {
                    std::string extension = entry.path().extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(),
                                   [](unsigned char c) { return std::tolower(c); });
                    if (extension == ".ttf" || extension == ".otf") {
                        m_fontFilePaths.push_back(entry.path().string());
                    }
                }
            }
            // Sort to ensure consistent loading order (e.g., Arial first)
            std::sort(m_fontFilePaths.begin(), m_fontFilePaths.end());
        } catch (const std::filesystem::filesystem_error& e) {
            FONT_ERROR("Filesystem error while scanning for fonts: " + std::string(e.what()));
            return false;
        }
    }

    // Calculate font sizes
    #ifdef __APPLE__
    float baseSizeFloat = APPLE_BASE_FONT_SIZE;
    #else
    int clampedHeight = std::clamp(windowHeight, 480, 8640);
    float baseSizeFloat = static_cast<float>(clampedHeight) / NON_APPLE_HEIGHT_RATIO;
    #endif

    int baseFontSize = std::clamp(static_cast<int>(std::round(baseSizeFloat)), MIN_BASE_FONT_SIZE, MAX_FONT_SIZE);
    int uiFontSize = std::clamp(static_cast<int>(std::round(baseSizeFloat * UI_FONT_RATIO)), MIN_UI_FONT_SIZE, MAX_FONT_SIZE);
    int titleFontSize = std::clamp(static_cast<int>(std::round(baseSizeFloat * TITLE_FONT_RATIO)), MIN_TITLE_FONT_SIZE, MAX_FONT_SIZE);
    int tooltipFontSize = std::clamp(static_cast<int>(std::round(baseSizeFloat * TOOLTIP_FONT_RATIO)), MIN_TOOLTIP_FONT_SIZE, MAX_FONT_SIZE);

    FONT_INFO("Calculated font sizes: base=" + std::to_string(baseFontSize) +
              ", UI=" + std::to_string(uiFontSize) + ", title=" + std::to_string(titleFontSize) +
              ", tooltip=" + std::to_string(tooltipFontSize));

    bool success = true;
    for (const auto& filePath : m_fontFilePaths) {
        std::string filename = std::filesystem::path(filePath).stem().string();
        success &= loadFont(filePath, "fonts_" + filename, baseFontSize);
        success &= loadFont(filePath, "fonts_UI_" + filename, uiFontSize);
        success &= loadFont(filePath, "fonts_title_" + filename, titleFontSize);
        success &= loadFont(filePath, "fonts_tooltip_" + filename, tooltipFontSize);
    }

    if (success) {
        m_lastWindowWidth = windowWidth;
        m_lastWindowHeight = windowHeight;
        m_lastFontPath = fontPath;
        m_fontsLoaded.store(true, std::memory_order_release);
        FONT_INFO("All font templates loaded successfully.");
    }

    return success;
}

bool FontManager::loadFont(const std::string& fontFile, const std::string& fontID, int fontSize) {
    auto font = std::shared_ptr<TTF_Font>(TTF_OpenFont(fontFile.c_str(), fontSize), TTF_CloseFont);

    if (!font) {
        FONT_ERROR("Failed to load font '" + fontFile + "' with size " + std::to_string(fontSize) + ": " + std::string(SDL_GetError()));
        return false;
    }

    TTF_SetFontHinting(font.get(), TTF_HINTING_NORMAL);
    TTF_SetFontKerning(font.get(), 1);
    TTF_SetFontStyle(font.get(), TTF_STYLE_NORMAL);

    m_fontMap[fontID] = std::move(font);
    FONT_INFO("Loaded font '" + fontID + "' from '" + fontFile + "'");
    return true;
}

std::shared_ptr<SDL_Texture> FontManager::renderText(
                                     const std::string& text, const std::string& fontID,
                                     SDL_Color color, SDL_Renderer* renderer) {
  // Skip if we're shutting down
  if (m_isShutdown) {
    FONT_WARN("Attempted to use FontManager after shutdown");
    return nullptr;
  }

  // Check cache first
  TextCacheKey key = {text, fontID, color};
  auto cacheIt = m_textCache.find(key);
  if (cacheIt != m_textCache.end()) {
    return cacheIt->second;
  }

  auto fontIt = m_fontMap.find(fontID);
  if (fontIt == m_fontMap.end()) {
    FONT_ERROR("Font '" + fontID + "' not found");
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
    FONT_ERROR("Failed to render text: " + std::string(SDL_GetError()));
    return nullptr;
  }

  // Create texture from surface with immediate RAII
  auto texture = std::shared_ptr<SDL_Texture>(
      SDL_CreateTextureFromSurface(renderer, surface.get()), SDL_DestroyTexture);

  if (!texture) {
    FONT_ERROR("Failed to create texture from rendered text: " + std::string(SDL_GetError()));
    return nullptr;
  }

  // Set texture scale mode for crisp font rendering - use NEAREST to avoid blur when scaling
  SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST);

  // Store in cache
  m_textCache[key] = texture;

  return texture;
}

SDL_Texture* FontManager::renderText(const std::string& text, const std::string& fontID,
                                     SDL_Color color, SDL_Renderer* renderer, bool) {
    if (text.empty()) {
        return nullptr;
    }

    auto it = m_fontMap.find(fontID);
    if (it == m_fontMap.end()) {
        FONT_ERROR("Font not found: " + fontID);
        return nullptr;
    }

    SDL_Surface* surface = TTF_RenderText_Blended(it->second.get(), text.c_str(), 0, color);
    if (!surface) {
        FONT_ERROR("Failed to render text surface: " + std::string(SDL_GetError()));
        return nullptr;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);

    if (!texture) {
        FONT_ERROR("Failed to create text texture: " + std::string(SDL_GetError()));
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
    FONT_ERROR("Failed to create combined surface: " + std::string(SDL_GetError()));
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
    FONT_ERROR("Failed to create texture from multi-line text: " + std::string(SDL_GetError()));
    return nullptr;
  }

  // Set texture blend mode to preserve alpha
  SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
  // Set texture scale mode for crisp font rendering - use NEAREST to avoid blur when scaling
  SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST);

  return texture;
}

void FontManager::drawText(const std::string& text, const std::string& fontID,
                          int x, int y, SDL_Color color, SDL_Renderer* renderer) {
  // Skip if we're shutting down
  if (m_isShutdown) {
    FONT_WARN("Attempted to use FontManager after shutdown");
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
  // Round coordinates to nearest pixel for crisp rendering
  SDL_FRect dstRect = {
    std::roundf(static_cast<float>(x - width/2.0f)), 
    std::roundf(static_cast<float>(y - height/2.0f)),
    static_cast<float>(width), 
    static_cast<float>(height)
  };

  // Render the texture
  SDL_RenderTexture(renderer, texture.get(), nullptr, &dstRect);

  // The texture will be automatically cleaned up when the unique_ptr goes out of scope
}

void FontManager::drawTextAligned(const std::string& text, const std::string& fontID,
                                 int x, int y, SDL_Color color, SDL_Renderer* renderer,
                                 int alignment) {
  // Skip if we're shutting down
  if (m_isShutdown) {
    FONT_WARN("Attempted to use FontManager after shutdown");
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

  // Create a destination rectangle with pixel-aligned coordinates
  SDL_FRect dstRect = {
    std::roundf(destX), 
    std::roundf(destY),
    static_cast<float>(width), 
    static_cast<float>(height)
  };

  // Render the texture using logical coordinates
  SDL_RenderTexture(renderer, texture.get(), nullptr, &dstRect);

  // The texture will be automatically cleaned up when the unique_ptr goes out of scope
}

std::vector<std::string> FontManager::wrapTextToLines(const std::string& text, 
                                                     const std::string& fontID, 
                                                     int maxWidth) {
  std::vector<std::string> wrappedLines;
  
  if (m_isShutdown || maxWidth <= 0) {
    wrappedLines.push_back(text); // Return original text if invalid params
    return wrappedLines;
  }

  auto fontIt = m_fontMap.find(fontID);
  if (fontIt == m_fontMap.end()) {
    FONT_ERROR("Font '" + fontID + "' not found for text wrapping");
    wrappedLines.push_back(text);
    return wrappedLines;
  }

  // First split by explicit newlines
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

  // Now wrap each line if it's too wide
  for (const auto& line : lines) {
    if (line.empty()) {
      wrappedLines.push_back("");
      continue;
    }

    std::string workingLine;
    std::istringstream words(line);
    std::string word;
    
    while (words >> word) {
      std::string testLine = workingLine.empty() ? word : workingLine + " " + word;
      int testWidth = 0;
      
      if (TTF_GetStringSize(fontIt->second.get(), testLine.c_str(), 0, &testWidth, nullptr)) {
        if (testWidth <= maxWidth) {
          workingLine = testLine;
        } else {
          if (!workingLine.empty()) {
            wrappedLines.push_back(workingLine);
            workingLine = word;
          } else {
            // Single word is too long, just add it
            wrappedLines.push_back(word);
            workingLine.clear();
          }
        }
      } else {
        // If measurement fails, just add the word
        workingLine = testLine;
      }
    }
    
    if (!workingLine.empty()) {
      wrappedLines.push_back(workingLine);
    }
  }

  return wrappedLines;
}

bool FontManager::measureTextWithWrapping(const std::string& text, const std::string& fontID,
                                         int maxWidth, int* width, int* height) {
  if (m_isShutdown || !width || !height) {
    return false;
  }

  auto fontIt = m_fontMap.find(fontID);
  if (fontIt == m_fontMap.end()) {
    FONT_ERROR("Font '" + fontID + "' not found for wrapped measurement");
    return false;
  }

  auto wrappedLines = wrapTextToLines(text, fontID, maxWidth);
  if (wrappedLines.empty()) {
    *width = 0;
    *height = 0;
    return true;
  }

  TTF_Font* font = fontIt->second.get();
  int lineHeight = TTF_GetFontHeight(font);
  int maxLineWidth = 0;

  // Measure each wrapped line to get the actual maximum width
  for (const auto& line : wrappedLines) {
    int lineWidth = 0;
    if (!line.empty()) {
      if (TTF_GetStringSize(font, line.c_str(), 0, &lineWidth, nullptr)) {
        maxLineWidth = std::max(maxLineWidth, lineWidth);
      }
    }
  }

  *width = maxLineWidth;
  *height = lineHeight * static_cast<int>(wrappedLines.size());
  return true;
}

void FontManager::drawTextWithWrapping(const std::string& text, const std::string& fontID,
                                      int x, int y, int maxWidth, SDL_Color color, 
                                      SDL_Renderer* renderer) {
  if (m_isShutdown || !renderer) {
    FONT_WARN("Attempted to draw wrapped text with invalid parameters");
    return;
  }

  auto fontIt = m_fontMap.find(fontID);
  if (fontIt == m_fontMap.end()) {
    FONT_ERROR("Font '" + fontID + "' not found for wrapped drawing");
    return;
  }

  auto wrappedLines = wrapTextToLines(text, fontID, maxWidth);
  TTF_Font* font = fontIt->second.get();
  int lineHeight = TTF_GetFontHeight(font);
  int currentY = y;

  // Draw each wrapped line
  for (const auto& line : wrappedLines) {
    if (!line.empty()) {
      auto texture = renderText(line, fontID, color, renderer);
      if (texture) {
        float w, h;
        SDL_GetTextureSize(texture.get(), &w, &h);
        
        SDL_FRect dstRect = {
          static_cast<float>(x), 
          static_cast<float>(currentY),
          w, h
        };
        
        SDL_RenderTexture(renderer, texture.get(), nullptr, &dstRect);
      }
    }
    currentY += lineHeight;
  }
}

bool FontManager::isFontLoaded(const std::string& fontID) const {
  return m_fontMap.find(fontID) != m_fontMap.end();
}

void FontManager::clearFont(const std::string& fontID) {
  // No need to manually call TTF_CloseFont as the unique_ptr will handle it
  if (m_fontMap.erase(fontID) > 0) {
    FONT_INFO("Cleared font: " + fontID);
  }
}

bool FontManager::reloadFontsForDisplay(const std::string& fontPath, int windowWidth, int windowHeight) {
  if (m_isShutdown) {
    FONT_WARN("Cannot reload fonts - FontManager is shut down");
    return false;
  }

  FONT_INFO("Reloading fonts for display change...");
  
  // Clear existing fonts and caches without shutting down the manager
  m_fontMap.clear();
  m_textCache.clear();
  m_fontsLoaded.store(false, std::memory_order_release);
  
  // Reset display tracking
  m_lastWindowWidth = 0;
  m_lastWindowHeight = 0;
  m_lastFontPath.clear();
  
  // Reload fonts with new dimensions
  return loadFontsForDisplay(fontPath, windowWidth, windowHeight);
}

bool FontManager::measureText(const std::string& text, const std::string& fontID, int* width, int* height) {
  if (m_isShutdown || !width || !height) {
    return false;
  }

  auto fontIt = m_fontMap.find(fontID);
  if (fontIt == m_fontMap.end()) {
    FONT_ERROR("Font '" + fontID + "' not found for measurement");
    return false;
  }

  // Use TTF_GetStringSize for accurate text measurement
  return TTF_GetStringSize(fontIt->second.get(), text.c_str(), 0, width, height);
}

bool FontManager::getFontMetrics(const std::string& fontID, int* lineHeight, int* ascent, int* descent) {
  if (m_isShutdown || !lineHeight || !ascent || !descent) {
    return false;
  }

  auto fontIt = m_fontMap.find(fontID);
  if (fontIt == m_fontMap.end()) {
    FONT_ERROR("Font '" + fontID + "' not found for metrics");
    return false;
  }

  TTF_Font* font = fontIt->second.get();
  *lineHeight = TTF_GetFontHeight(font);
  *ascent = TTF_GetFontAscent(font);
  *descent = TTF_GetFontDescent(font);
  
  return true;
}

bool FontManager::measureMultilineText(const std::string& text, const std::string& fontID, 
                                      int maxWidth, int* width, int* height) {
  if (m_isShutdown || !width || !height) {
    return false;
  }

  auto fontIt = m_fontMap.find(fontID);
  if (fontIt == m_fontMap.end()) {
    FONT_ERROR("Font '" + fontID + "' not found for multiline measurement");
    return false;
  }

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
    *width = 0;
    *height = 0;
    return true;
  }

  TTF_Font* font = fontIt->second.get();
  int lineHeight = TTF_GetFontHeight(font);
  int maxLineWidth = 0;

  // Measure each line
  for (const auto& line : lines) {
    int lineWidth = 0;
    if (!line.empty()) {
      if (!TTF_GetStringSize(font, line.c_str(), 0, &lineWidth, nullptr)) {
        FONT_ERROR("Failed to measure line: " + line);
        return false;
      }
    }
    maxLineWidth = std::max(maxLineWidth, lineWidth);
  }

  // Apply max width constraint if specified
  if (maxWidth > 0 && maxLineWidth > maxWidth) {
    maxLineWidth = maxWidth;
  }

  *width = maxLineWidth;
  *height = lineHeight * static_cast<int>(lines.size());
  
  return true;
}

void FontManager::clean() {
  if (m_isShutdown) {
    return;
  }

  // Track the number of fonts cleaned up
  [[maybe_unused]] int fontsFreed = m_fontMap.size();
  // Mark the manager as shutting down before freeing resources
  m_isShutdown = true;

  // No need to manually close fonts as the unique_ptr will handle it
  m_fontMap.clear();
  m_textCache.clear();
  
  // Clear display tracking
  m_lastWindowWidth = 0;
  m_lastWindowHeight = 0;
  m_lastFontPath.clear();

  FONT_INFO(std::to_string(fontsFreed) + " fonts freed");
  FONT_INFO("FontManager resources cleaned - TTF will be cleaned by SDL_Quit()");
}
