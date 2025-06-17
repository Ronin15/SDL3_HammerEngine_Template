/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef FONT_MANAGER_HPP
#define FONT_MANAGER_HPP

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>
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

  /**
   * @brief Initializes the TTF font system
   * @return true if initialization successful, false otherwise
   */
  bool init();

  /**
   * @brief Loads a font with specified size from file or directory
   * @param fontFile Path to font file or directory containing TTF/OTF files
   * @param fontID Unique identifier for the font(s). Used as prefix when loading directory
   * @param fontSize Size of the font in points
   * @return true if at least one font was loaded successfully, false otherwise
   */
  bool loadFont(const std::string& fontFile, const std::string& fontID, int fontSize);

  /**
   * @brief Loads default fonts with sizes calculated based on display characteristics
   * @param fontPath Path to font file or directory containing TTF/OTF files
   * @param windowWidth Current window width in pixels
   * @param windowHeight Current window height in pixels
   * @return true if fonts were loaded successfully, false otherwise
   */
  bool loadFontsForDisplay(const std::string& fontPath, int windowWidth, int windowHeight);

  /**
   * @brief Refreshes existing fonts with new sizes based on updated display characteristics
   * @param fontPath Path to font file or directory containing TTF/OTF files
   * @param windowWidth New window width in pixels
   * @param windowHeight New window height in pixels
   * @return true if fonts were refreshed successfully, false otherwise
   */
  bool refreshFontsForDisplay(const std::string& fontPath, int windowWidth, int windowHeight);

  /**
   * @brief Renders text to a texture using specified font
   * @param text Text string to render
   * @param fontID Unique identifier of the font to use
   * @param color Text color for rendering
   * @param renderer SDL renderer for texture creation
   * @return Shared pointer to rendered text texture, or nullptr if failed
   */
  std::shared_ptr<SDL_Texture> renderText(
                          const std::string& text, const std::string& fontID,
                          SDL_Color color, SDL_Renderer* renderer);

  /**
   * @brief Renders multi-line text to a texture (handles newlines)
   * @param text Multi-line text string to render
   * @param font TTF font to use for rendering
   * @param color Text color for rendering
   * @param renderer SDL renderer for texture creation
   * @return Shared pointer to rendered text texture, or nullptr if failed
   */
  std::shared_ptr<SDL_Texture> renderMultiLineText(
                          const std::string& text, TTF_Font* font,
                          SDL_Color color, SDL_Renderer* renderer);

  /**
   * @brief Draws text directly to renderer at center position
   * @param text Text string to draw
   * @param fontID Unique identifier of the font to use
   * @param x X coordinate (center point of text)
   * @param y Y coordinate (center point of text)
   * @param color Text color for drawing
   * @param renderer SDL renderer to draw to
   */
  void drawText(const std::string& text, const std::string& fontID,
                int x, int y, SDL_Color color, SDL_Renderer* renderer);

  /**
   * @brief Draws text with alignment control for UI elements
   * @param text Text string to draw
   * @param fontID Unique identifier of the font to use
   * @param x X coordinate for positioning
   * @param y Y coordinate for positioning
   * @param color Text color for drawing
   * @param renderer SDL renderer to draw to
   * @param alignment Text alignment (0=center, 1=left, 2=right, 3=top-left, 4=top-center, 5=top-right)
   */
  void drawTextAligned(const std::string& text, const std::string& fontID,
                      int x, int y, SDL_Color color, SDL_Renderer* renderer,
                      int alignment = 0);

  /**
   * @brief Checks if a font is loaded in memory
   * @param fontID Unique identifier of the font to check
   * @return true if font is loaded, false otherwise
   */
  bool isFontLoaded(const std::string& fontID) const;

  /**
   * @brief Removes a specific font from memory
   * @param fontID Unique identifier of the font to remove
   */
  void clearFont(const std::string& fontID);

  /**
   * @brief Cleans up all font resources and shuts down TTF system
   */
  void clean();

  /**
   * @brief Checks if FontManager has been shut down
   * @return true if manager is shut down, false otherwise
   */
  bool isShutdown() const { return m_isShutdown; }

  /**
   * @brief Measures text dimensions for a given font and string
   * @param text Text string to measure
   * @param fontID Font identifier to use for measurement
   * @param width Pointer to store calculated width
   * @param height Pointer to store calculated height
   * @return true if measurement successful, false otherwise
   */
  bool measureText(const std::string& text, const std::string& fontID, int* width, int* height);

  /**
   * @brief Gets font metrics (line height, etc.) for auto-sizing calculations
   * @param fontID Font identifier to get metrics for
   * @param lineHeight Pointer to store line height
   * @param ascent Pointer to store font ascent
   * @param descent Pointer to store font descent
   * @return true if metrics retrieved successfully, false otherwise
   */
  bool getFontMetrics(const std::string& fontID, int* lineHeight, int* ascent, int* descent);

  /**
   * @brief Calculates optimal size for multi-line text content
   * @param text Multi-line text string (may contain newlines)
   * @param fontID Font identifier to use
   * @param maxWidth Maximum width constraint (0 = no limit)
   * @param width Pointer to store calculated total width
   * @param height Pointer to store calculated total height
   * @return true if calculation successful, false otherwise
   */
  bool measureMultilineText(const std::string& text, const std::string& fontID, 
                           int maxWidth, int* width, int* height);

  /**
   * @brief Measure text with word wrapping
   * @param text Text to measure
   * @param fontID Font identifier to use
   * @param maxWidth Maximum width for wrapping
   * @param width Pointer to store calculated total width
   * @param height Pointer to store calculated total height
   * @return true if calculation successful, false otherwise
   */
  bool measureTextWithWrapping(const std::string& text, const std::string& fontID,
                              int maxWidth, int* width, int* height);

  /**
   * @brief Draw text with word wrapping
   * @param text Text to draw
   * @param fontID Font identifier to use
   * @param x X position for text
   * @param y Y position for text
   * @param maxWidth Maximum width for wrapping
   * @param color Text color
   * @param renderer SDL renderer to draw to
   */
  void drawTextWithWrapping(const std::string& text, const std::string& fontID,
                           int x, int y, int maxWidth, SDL_Color color, 
                           SDL_Renderer* renderer);

  /**
   * @brief Wrap text into lines that fit within specified width
   * @param text Text to wrap
   * @param fontID Font identifier to use
   * @param maxWidth Maximum width constraint
   * @return Vector of wrapped lines
   */
  std::vector<std::string> wrapTextToLines(const std::string& text, 
                                          const std::string& fontID, 
                                          int maxWidth);

 private:
  std::unordered_map<std::string, std::shared_ptr<TTF_Font>> m_fontMap{};
  bool m_isShutdown{false}; // Flag to indicate if FontManager has been shut down

  // Delete copy constructor and assignment operator
  FontManager(const FontManager&) = delete; // Prevent copying
  FontManager& operator=(const FontManager&) = delete; // Prevent assignment

  FontManager() = default;
};

#endif  // FONT_MANAGER_HPP
