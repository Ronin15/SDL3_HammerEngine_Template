// Copyright (c) 2025 Hammer Forged Games
// Licensed under the MIT License - see LICENSE file for details

#ifndef FONT_MANAGER_HPP
#define FONT_MANAGER_HPP

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <boost/container/flat_map.hpp>
#include <string>
// filesystem is used in the implementation file

class FontManager {
 public:
  ~FontManager() {
    if (!m_isShutdown) {
      clean();
    }
  }

  static FontManager& Instance() {
    static FontManager instance;
    return instance;
  }

  // Initialize the font system
  bool init();

  // Load a font with a specific size
  // If fontFile is a directory, it loads all TTF/OTF files from that directory
  // Returns true if at least one font was loaded successfully
  bool loadFont(const std::string& fontFile, const std::string& fontID, int fontSize);

  // Render text to a texture
  SDL_Texture* renderText(const std::string& text, const std::string& fontID,
                          SDL_Color color, SDL_Renderer* renderer);

  // Draw text directly to renderer
  void drawText(const std::string& text, const std::string& fontID,
                int x, int y, SDL_Color color, SDL_Renderer* renderer);

  // Check if a font is loaded
  bool isFontLoaded(const std::string& fontID) const;

  // Clear a specific font from memory
  void clearFont(const std::string& fontID);

  // Clean up all font resources
  void clean();
  
  // Check if FontManager has been shut down
  bool isShutdown() const { return m_isShutdown; }

 private:
  boost::container::flat_map<std::string, TTF_Font*> m_fontMap;
  static TTF_TextEngine* m_rendererTextEngine;
  bool m_isShutdown = false; // Flag to indicate if FontManager has been shut down
  
  FontManager() {} // Private constructor for singleton
  FontManager(const FontManager&) = delete; // Prevent copying
  FontManager& operator=(const FontManager&) = delete; // Prevent assignment
};

#endif  // FONT_MANAGER_HPP
