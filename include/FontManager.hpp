#ifndef FONT_MANAGER_HPP
#define FONT_MANAGER_HPP

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <boost/container/flat_map.hpp>
#include <string>
// filesystem is used in the implementation file

class FontManager {
 public:
  FontManager() {}
  ~FontManager() {}

  static FontManager& Instance(){
        static FontManager* sp_instance = new FontManager();
        return *sp_instance;
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

 private:
  boost::container::flat_map<std::string, TTF_Font*> m_fontMap;
  static FontManager* sp_Instance;
  static TTF_TextEngine* m_rendererTextEngine;
};

#endif  // FONT_MANAGER_HPP
