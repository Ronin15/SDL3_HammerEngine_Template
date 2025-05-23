/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef TEXTURE_MANAGER_HPP
#define TEXTURE_MANAGER_HPP

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <boost/container/flat_map.hpp>
#include <string>

class TextureManager {
 public:
 ~TextureManager() = default;

 static TextureManager& Instance() {
   static TextureManager instance;
   initialized = true;
   return instance;
 }
 static bool Exists() { return initialized; }

  // Loads a texture from a file or all PNG textures from a directory
  // If fileName is a directory, it loads all PNG files from that directory
  // Returns true if at least one texture was loaded successfully
  // When loading a directory, textureID is used as a prefix for filenames
  bool load(std::string fileName,
            std::string textureID,
            SDL_Renderer* p_renderer);

  void draw(const std::string& textureID,
            int x,
            int y,
            int width,
            int height,
            SDL_Renderer* p_Renderer,
            SDL_FlipMode flip = SDL_FLIP_NONE);

  void drawFrame(const std::string& textureID,
                 int x,
                 int y,
                 int width,
                 int height,
                 int currentRow,
                 int currentFrame,
                 SDL_Renderer* p_renderer,
                 SDL_FlipMode flip = SDL_FLIP_NONE);
  void drawParallax(const std::string& textureID,
                    int x,
                    int y,
                    int scroll,
                    SDL_Renderer* p_renderer);
  void clearFromTexMap(const std::string& textureID);
  bool isTextureInMap(const std::string& textureID) const;

  // Get a texture pointer by ID
  SDL_Texture* getTexture(const std::string& textureID) const;

  // Clean up all texture resources
  void clean();

 private:
  std::string m_textureID{""};
  boost::container::flat_map<std::string, SDL_Texture*> m_textureMap{};
  static bool initialized;

  // Delete copy constructor and assignment operator
  TextureManager(const TextureManager&) = delete; //prevent copy construction
  TextureManager& operator=(const TextureManager&) = delete; //prevent assignment

  TextureManager() = default;

};

#endif  // TEXTURE_MANAGER_HPP
