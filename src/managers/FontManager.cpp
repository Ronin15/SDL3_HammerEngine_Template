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
#include <format>

// Font sizing configuration constants
namespace {
  // Universal height-based font sizing (all platforms)
  constexpr float HEIGHT_RATIO = 90.0f;

  // Font size ratios for different text types
  constexpr float UI_FONT_RATIO = 0.875f;     // 87.5% of base
  constexpr float TITLE_FONT_RATIO = 1.5f;    // 150% of base
  constexpr float TOOLTIP_FONT_RATIO = 0.6f;  // 60% of base

  // Font size bounds for edge case protection
  constexpr int MAX_FONT_SIZE = 100;

  // Minimum readable sizes for specific font types (ensure good readability)
  constexpr int MIN_BASE_FONT_SIZE = 18;
  constexpr int MIN_UI_FONT_SIZE = 16;
  constexpr int MIN_TITLE_FONT_SIZE = 24;
  constexpr int MIN_TOOLTIP_FONT_SIZE = 12;
}

bool FontManager::init() {
  if (!TTF_Init()) {
    FONT_CRITICAL(std::format("Font system initialization failed: {}", SDL_GetError()));
      return false;
  } else {
    // Reset shutdown flag when reinitializing
    m_isShutdown = false;
    FONT_INFO("Font system initialized with quality hints!");
      return true;
  }
}

bool FontManager::loadFontsForDisplay(const std::string& fontPath, int windowWidth, int windowHeight, float dpiScale) {
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
            FONT_ERROR(std::format("Filesystem error while scanning for fonts: {}", e.what()));
            return false;
        }
    }

    // Ensure dpiScale is valid
    float const effectiveDpiScale = (dpiScale > 0.0f) ? dpiScale : 1.0f;

    // Calculate font sizes based on LOGICAL window height first
    // Apply minimums for readability, THEN scale by DPI for high-density displays
    int clampedHeight = std::clamp(windowHeight, 480, 8640);
    float const logicalBaseSizeFloat = static_cast<float>(clampedHeight) / HEIGHT_RATIO;

    // Apply minimums at logical scale, then multiply by DPI scale for pixel rendering
    int logicalBase = std::max(static_cast<int>(std::round(logicalBaseSizeFloat)), MIN_BASE_FONT_SIZE);
    int logicalUI = std::max(static_cast<int>(std::round(logicalBaseSizeFloat * UI_FONT_RATIO)), MIN_UI_FONT_SIZE);
    int logicalTitle = std::max(static_cast<int>(std::round(logicalBaseSizeFloat * TITLE_FONT_RATIO)), MIN_TITLE_FONT_SIZE);
    int logicalTooltip = std::max(static_cast<int>(std::round(logicalBaseSizeFloat * TOOLTIP_FONT_RATIO)), MIN_TOOLTIP_FONT_SIZE);

    // Scale by DPI for pixel-perfect rendering on high-density displays
    int baseFontSize = std::min(static_cast<int>(std::round(logicalBase * effectiveDpiScale)), MAX_FONT_SIZE);
    int uiFontSize = std::min(static_cast<int>(std::round(logicalUI * effectiveDpiScale)), MAX_FONT_SIZE);
    int titleFontSize = std::min(static_cast<int>(std::round(logicalTitle * effectiveDpiScale)), MAX_FONT_SIZE);
    int tooltipFontSize = std::min(static_cast<int>(std::round(logicalTooltip * effectiveDpiScale)), MAX_FONT_SIZE);

    FONT_INFO(std::format("Calculated font sizes (dpiScale={}, logical={}): base={}, UI={}, title={}, tooltip={}",
              effectiveDpiScale, logicalBase, baseFontSize, uiFontSize, titleFontSize, tooltipFontSize));

    bool success = true;
    for (const auto& filePath : m_fontFilePaths) {
        std::string filename = std::filesystem::path(filePath).stem().string();
        success &= loadFont(filePath, std::format("fonts_{}", filename), baseFontSize);
        success &= loadFont(filePath, std::format("fonts_UI_{}", filename), uiFontSize);
        success &= loadFont(filePath, std::format("fonts_title_{}", filename), titleFontSize);
        success &= loadFont(filePath, std::format("fonts_tooltip_{}", filename), tooltipFontSize);
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
        FONT_ERROR(std::format("Failed to load font '{}' with size {}: {}", fontFile, fontSize, SDL_GetError()));
        return false;
    }

    TTF_SetFontHinting(font.get(), TTF_HINTING_NORMAL);
    TTF_SetFontKerning(font.get(), 1);
    TTF_SetFontStyle(font.get(), TTF_STYLE_NORMAL);

    m_fontMap[fontID] = std::move(font);
    FONT_INFO(std::format("Loaded font '{}' from '{}'", fontID, fontFile));
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
    FONT_ERROR(std::format("Font '{}' not found", fontID));
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
    FONT_ERROR(std::format("Failed to render text: {}", SDL_GetError()));
    return nullptr;
  }

  // Create texture from surface with immediate RAII
  auto texture = std::shared_ptr<SDL_Texture>(
      SDL_CreateTextureFromSurface(renderer, surface.get()), SDL_DestroyTexture);

  if (!texture) {
    FONT_ERROR(std::format("Failed to create texture from rendered text: {}", SDL_GetError()));
    return nullptr;
  }

  // Set texture scale mode for smooth font rendering - LINEAR provides antialiased scaling
  SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR);

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
        FONT_ERROR(std::format("Font not found: {}", fontID));
        return nullptr;
    }

    SDL_Surface* surface = TTF_RenderText_Blended(it->second.get(), text.c_str(), 0, color);
    if (!surface) {
        FONT_ERROR(std::format("Failed to render text surface: {}", SDL_GetError()));
        return nullptr;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);

    if (!texture) {
        FONT_ERROR(std::format("Failed to create text texture: {}", SDL_GetError()));
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
  int const lineHeight = TTF_GetFontHeight(font);

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
    FONT_ERROR(std::format("Failed to create combined surface: {}", SDL_GetError()));
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
    FONT_ERROR(std::format("Failed to create texture from multi-line text: {}", SDL_GetError()));
    return nullptr;
  }

  // Set texture blend mode to preserve alpha
  SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
  // Set texture scale mode for smooth font rendering - LINEAR provides antialiased scaling
  SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR);

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
  int const width = static_cast<int>(w);
  int const height = static_cast<int>(h);

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
  int const width = static_cast<int>(w);
  int const height = static_cast<int>(h);

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
    FONT_ERROR(std::format("Font '{}' not found for text wrapping", fontID));
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
      std::string testLine = workingLine;
      if (!testLine.empty()) {
        testLine += " ";
      }
      testLine += word;
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
    FONT_ERROR(std::format("Font '{}' not found for wrapped measurement", fontID));
    return false;
  }

  auto wrappedLines = wrapTextToLines(text, fontID, maxWidth);
  if (wrappedLines.empty()) {
    *width = 0;
    *height = 0;
    return true;
  }

  TTF_Font* font = fontIt->second.get();
  int const lineHeight = TTF_GetFontHeight(font);
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
    FONT_ERROR(std::format("Font '{}' not found for wrapped drawing", fontID));
    return;
  }

  auto wrappedLines = wrapTextToLines(text, fontID, maxWidth);
  TTF_Font* font = fontIt->second.get();
  int const lineHeight = TTF_GetFontHeight(font);
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
    FONT_INFO(std::format("Cleared font: {}", fontID));
  }
}

bool FontManager::reloadFontsForDisplay(const std::string& fontPath, int windowWidth, int windowHeight, float dpiScale) {
  if (m_isShutdown) {
    FONT_WARN("Cannot reload fonts - FontManager is shut down");
    return false;
  }

  FONT_INFO("Reloading fonts for display change...");

  // Clear existing fonts and caches without shutting down the manager
  m_fontMap.clear();
  m_textCache.clear();
#ifdef USE_SDL3_GPU
  m_gpuTextCache.clear();
#endif
  m_fontsLoaded.store(false, std::memory_order_release);

  // Reset display tracking
  m_lastWindowWidth = 0;
  m_lastWindowHeight = 0;
  m_lastFontPath.clear();

  // Reload fonts with new dimensions and DPI scale
  return loadFontsForDisplay(fontPath, windowWidth, windowHeight, dpiScale);
}

bool FontManager::measureText(const std::string& text, const std::string& fontID, int* width, int* height) {
  if (m_isShutdown || !width || !height) {
    return false;
  }

  auto fontIt = m_fontMap.find(fontID);
  if (fontIt == m_fontMap.end()) {
    FONT_ERROR(std::format("Font '{}' not found for measurement", fontID));
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
    FONT_ERROR(std::format("Font '{}' not found for metrics", fontID));
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
    FONT_ERROR(std::format("Font '{}' not found for multiline measurement", fontID));
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
  int const lineHeight = TTF_GetFontHeight(font);
  int maxLineWidth = 0;

  // Measure each line
  for (const auto& line : lines) {
    int lineWidth = 0;
    if (!line.empty()) {
      if (!TTF_GetStringSize(font, line.c_str(), 0, &lineWidth, nullptr)) {
        FONT_ERROR(std::format("Failed to measure line: {}", line));
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

#ifdef USE_SDL3_GPU
  // Clear GPU text cache (must be done before GPU device shutdown)
  m_gpuTextCache.clear();
#endif

  // Clear display tracking
  m_lastWindowWidth = 0;
  m_lastWindowHeight = 0;
  m_lastFontPath.clear();

  FONT_INFO(std::format("{} fonts freed", fontsFreed));
  FONT_INFO("FontManager resources cleaned - TTF will be cleaned by SDL_Quit()");
}

#ifdef USE_SDL3_GPU
#include "gpu/GPUDevice.hpp"
#include "gpu/GPUTransferBuffer.hpp"
#include "gpu/GPUTypes.hpp"
#include <cstring>

const GPUTextData* FontManager::renderTextGPU(const std::string& text, const std::string& fontID,
                                               SDL_Color color) {
  if (m_isShutdown || text.empty()) {
    return nullptr;
  }

  // Check GPU cache first
  TextCacheKey key = {text, fontID, color};
  auto cacheIt = m_gpuTextCache.find(key);
  if (cacheIt != m_gpuTextCache.end()) {
    return cacheIt->second.get();
  }

  auto fontIt = m_fontMap.find(fontID);
  if (fontIt == m_fontMap.end()) {
    FONT_ERROR(std::format("Font '{}' not found for GPU rendering", fontID));
    return nullptr;
  }

  // Render text to surface
  SDL_Surface* surface = TTF_RenderText_Blended(fontIt->second.get(), text.c_str(), 0, color);
  if (!surface) {
    FONT_ERROR(std::format("Failed to render text surface for GPU: {}", SDL_GetError()));
    return nullptr;
  }

  // Convert to ABGR8888 for GPU upload (maps to R8G8B8A8_UNORM byte order on little-endian)
  SDL_Surface* converted = nullptr;
  if (surface->format != SDL_PIXELFORMAT_ABGR8888) {
    converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(surface);
    if (!converted) {
      FONT_ERROR(std::format("Failed to convert text surface: {}", SDL_GetError()));
      return nullptr;
    }
    surface = converted;
  }

  // Premultiply alpha for proper blending
  SDL_PremultiplyAlpha(surface->w, surface->h,
                        SDL_PIXELFORMAT_ABGR8888, surface->pixels, surface->pitch,
                        SDL_PIXELFORMAT_ABGR8888, surface->pixels, surface->pitch,
                        false);

  // Create GPU texture data
  auto gpuData = std::make_unique<GPUTextData>();
  gpuData->width = surface->w;
  gpuData->height = surface->h;

  auto& gpuDevice = HammerEngine::GPUDevice::Instance();
  SDL_GPUDevice* device = gpuDevice.get();

  // Create GPU texture
  gpuData->texture = std::make_unique<HammerEngine::GPUTexture>(
      device,
      static_cast<uint32_t>(surface->w),
      static_cast<uint32_t>(surface->h),
      SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      SDL_GPU_TEXTUREUSAGE_SAMPLER
  );

  if (!gpuData->texture->isValid()) {
    FONT_ERROR("Failed to create GPU texture for text");
    SDL_DestroySurface(surface);
    return nullptr;
  }

  // Upload texture data immediately using a transfer buffer
  uint32_t dataSize = static_cast<uint32_t>(surface->pitch * surface->h);
  HammerEngine::GPUTransferBuffer transferBuf(device, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, dataSize);

  if (!transferBuf.isValid()) {
    FONT_ERROR("Failed to create transfer buffer for text upload");
    SDL_DestroySurface(surface);
    return nullptr;
  }

  // Copy pixel data to transfer buffer
  void* mapped = transferBuf.map(false);
  if (mapped) {
    std::memcpy(mapped, surface->pixels, dataSize);
    transferBuf.unmap();
  }

  // Upload using a one-time command buffer
  SDL_GPUCommandBuffer* cmdBuf = SDL_AcquireGPUCommandBuffer(device);
  if (cmdBuf) {
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuf);
    if (copyPass) {
      SDL_GPUTextureTransferInfo src{};
      src.transfer_buffer = transferBuf.get();
      src.offset = 0;
      src.pixels_per_row = static_cast<uint32_t>(surface->w);
      src.rows_per_layer = static_cast<uint32_t>(surface->h);

      SDL_GPUTextureRegion dst{};
      dst.texture = gpuData->texture->get();
      dst.w = static_cast<uint32_t>(surface->w);
      dst.h = static_cast<uint32_t>(surface->h);
      dst.d = 1;

      SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
      SDL_EndGPUCopyPass(copyPass);
    }
    SDL_SubmitGPUCommandBuffer(cmdBuf);
  }

  SDL_DestroySurface(surface);

  // Cache and return
  GPUTextData* result = gpuData.get();
  m_gpuTextCache[key] = std::move(gpuData);
  return result;
}

void FontManager::drawTextGPU(const std::string& text, const std::string& fontID,
                               int x, int y, SDL_Color color,
                               HammerEngine::GPURenderer& gpuRenderer,
                               SDL_GPURenderPass* pass) {
  if (!pass || text.empty()) {
    return;
  }

  const GPUTextData* textData = renderTextGPU(text, fontID, color);
  if (!textData || !textData->texture || !textData->texture->isValid()) {
    return;
  }

  // Calculate centered position
  float dstX = static_cast<float>(x - textData->width / 2);
  float dstY = static_cast<float>(y - textData->height / 2);
  float dstW = static_cast<float>(textData->width);
  float dstH = static_cast<float>(textData->height);

  // Create orthographic projection for screen-space rendering
  float orthoMatrix[16];
  HammerEngine::GPURenderer::createOrthoMatrix(
      0.0f, static_cast<float>(gpuRenderer.getViewportWidth()),
      static_cast<float>(gpuRenderer.getViewportHeight()), 0.0f,
      orthoMatrix);

  // Bind UI sprite pipeline (for swapchain rendering)
  SDL_BindGPUGraphicsPipeline(pass, gpuRenderer.getUISpritePipeline());

  // Push view-projection
  gpuRenderer.pushViewProjection(pass, orthoMatrix);

  // Create quad vertices
  HammerEngine::SpriteVertex vertices[4];
  // Top-left
  vertices[0] = {dstX, dstY, 0.0f, 0.0f, 255, 255, 255, 255};
  // Top-right
  vertices[1] = {dstX + dstW, dstY, 1.0f, 0.0f, 255, 255, 255, 255};
  // Bottom-right
  vertices[2] = {dstX + dstW, dstY + dstH, 1.0f, 1.0f, 255, 255, 255, 255};
  // Bottom-left
  vertices[3] = {dstX, dstY + dstH, 0.0f, 1.0f, 255, 255, 255, 255};

  // Use the sprite batch's index buffer for the quad
  const auto& batch = gpuRenderer.getSpriteBatch();

  // Bind texture
  SDL_GPUTextureSamplerBinding texSampler{};
  texSampler.texture = textData->texture->get();
  texSampler.sampler = gpuRenderer.getLinearSampler();
  SDL_BindGPUFragmentSamplers(pass, 0, &texSampler, 1);

  // Upload vertices using a small transfer buffer
  auto& gpuDevice = HammerEngine::GPUDevice::Instance();
  HammerEngine::GPUTransferBuffer vertexTransfer(
      gpuDevice.get(), SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, sizeof(vertices));

  if (vertexTransfer.isValid()) {
    void* mapped = vertexTransfer.map(false);
    if (mapped) {
      std::memcpy(mapped, vertices, sizeof(vertices));
      vertexTransfer.unmap();
    }
  }

  // For text, we use immediate drawing with the sprite batch index buffer
  // Bind the vertex buffer from sprite pool (it has our vertices)
  SDL_GPUBufferBinding vertexBinding{};
  vertexBinding.buffer = gpuRenderer.getSpriteVertexPool().getGPUBuffer();
  vertexBinding.offset = 0;
  SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);

  // Bind the index buffer
  SDL_GPUBufferBinding indexBinding{};
  indexBinding.buffer = batch.getIndexBuffer();
  indexBinding.offset = 0;
  SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

  // Draw the quad (6 indices for 4 vertices)
  SDL_DrawGPUIndexedPrimitives(pass, 6, 1, 0, 0, 0);
}

void FontManager::clearGPUTextCache() {
  m_gpuTextCache.clear();
  FONT_DEBUG("GPU text cache cleared");
}
#endif
